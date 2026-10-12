"""Async JSON-RPC IPC Service bridge between Kdenlive (C++/Qt6) and PydanticAI."""

from __future__ import annotations

import asyncio
import json
import os
import sys
from typing import Any, Dict, Optional

from .config import AgentConfig
from .agent import KdenliveDeps, build_vidmate_agent
from pydantic_ai.exceptions import ModelRetry


class VidMateAgentService:
    """Manages the IPC communication loop and PydanticAI agent runs."""

    def __init__(self, config: Optional[AgentConfig] = None):
        self.config = config or AgentConfig.load()
        self.pending_tool_responses: Dict[str, asyncio.Future[Dict[str, Any]]] = {}
        self._current_writer: Optional[asyncio.StreamWriter] = None
        self._agent = None
        self._seq = 0

    def _next_id(self) -> str:
        self._seq += 1
        return f"req_{self._seq}"

    async def _send_json(self, writer: asyncio.StreamWriter, payload: Dict[str, Any]) -> None:
        """Send newline-delimited JSON payload."""
        data = json.dumps(payload, ensure_ascii=False) + "\n"
        writer.write(data.encode("utf-8"))
        await writer.drain()

    async def execute_kdenlive_action(
        self,
        writer: asyncio.StreamWriter,
        action_name: str,
        params: Dict[str, Any],
    ) -> Dict[str, Any]:
        """Dispatch a tool call back to Kdenlive and await the outcome asynchronously."""
        call_id = self._next_id()
        fut: asyncio.Future[Dict[str, Any]] = asyncio.get_running_loop().create_future()
        self.pending_tool_responses[call_id] = fut

        # Emit tool dispatch notification to Kdenlive
        await self._send_json(writer, {
            "type": "tool_call",
            "call_id": call_id,
            "action": action_name,
            "parameters": params,
        })

        try:
            # Wait for Kdenlive to execute on timeline and return outcome
            result = await asyncio.wait_for(fut, timeout=120.0)
            return result
        except asyncio.TimeoutError:
            return {"success": False, "error": f"Tool execution timed out for {action_name}"}
        finally:
            self.pending_tool_responses.pop(call_id, None)

    async def handle_prompt_request(
        self,
        writer: asyncio.StreamWriter,
        req_id: str,
        prompt: str,
        editor_state: Dict[str, Any],
        provider: Optional[str] = None,
        model_name: Optional[str] = None,
    ) -> None:
        """Execute a full agent run turn with streaming and self-correction."""
        agent = build_vidmate_agent(self.config, provider=provider, model_name=model_name)

        async def _action_executor(action: str, params: Dict[str, Any]) -> Dict[str, Any]:
            return await self.execute_kdenlive_action(writer, action, params)

        deps = KdenliveDeps(
            action_executor=_action_executor,
            editor_state=editor_state,
            session_id=req_id,
        )

        try:
            # Format prompt with latest editor state
            user_content = prompt
            if editor_state:
                if isinstance(editor_state, dict) and "raw_snapshot" in editor_state:
                    state_str = str(editor_state["raw_snapshot"]).strip()
                else:
                    state_str = json.dumps(editor_state, indent=2)
                user_content = f"{prompt}\n\n{state_str}"

            await self._send_json(writer, {
                "type": "run_start",
                "req_id": req_id,
                "provider": provider or self.config.active_provider,
                "model": model_name or self.config.groq_model,
            })

            # Run agent with streaming tokens
            full_text = ""
            total_tokens = 0
            async with agent.run_stream(user_content, deps=deps) as result:
                async for text in result.stream_text(delta=True):
                    full_text += text
                    await self._send_json(writer, {
                        "type": "stream_delta",
                        "req_id": req_id,
                        "text": text,
                    })

                usage = getattr(result, "usage", None)
                if callable(usage):
                    usage = usage()
                if usage:
                    total_tokens = (getattr(usage, "total_tokens", 0) or
                                    (getattr(usage, "input_tokens", 0) or 0) + (getattr(usage, "output_tokens", 0) or 0))

            # Fallback approximate token estimation if provider returns 0
            if total_tokens <= 0:
                total_tokens = max(1, (len(user_content) + len(full_text)) // 4)

            await self._send_json(writer, {
                "type": "run_complete",
                "req_id": req_id,
                "success": True,
                "tokens": total_tokens,
                "summary": full_text.strip() or "Goal completed successfully.",
            })

        except Exception as e:
            await self._send_json(writer, {
                "type": "run_error",
                "req_id": req_id,
                "error": str(e),
            })

    async def handle_client(self, reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
        """Handle incoming line-delimited JSON-RPC from Kdenlive client."""
        while not reader.at_eof():
            try:
                line = await reader.readline()
                if not line:
                    break

                text = line.decode("utf-8").strip()
                if not text:
                    continue

                msg = json.loads(text)
                msg_type = msg.get("type")

                if msg_type == "ping":
                    await self._send_json(writer, {
                        "type": "pong",
                        "status": "ready",
                        "active_provider": self.config.active_provider,
                    })

                elif msg_type == "prompt":
                    req_id = msg.get("req_id", self._next_id())
                    prompt = msg.get("prompt", "")
                    editor_state = msg.get("editor_state", {})
                    provider = msg.get("provider")
                    model = msg.get("model")
                    asyncio.create_task(
                        self.handle_prompt_request(writer, req_id, prompt, editor_state, provider, model)
                    )

                elif msg_type == "tool_response":
                    call_id = msg.get("call_id")
                    if call_id and call_id in self.pending_tool_responses:
                        fut = self.pending_tool_responses[call_id]
                        if not fut.done():
                            fut.set_result(msg.get("result", {}))

            except json.JSONDecodeError:
                await self._send_json(writer, {"type": "error", "message": "Malformed JSON"})
            except Exception as e:
                await self._send_json(writer, {"type": "error", "message": str(e)})

        writer.close()
        await writer.wait_closed()

    async def run_socket_server(self) -> None:
        """Start listening on Unix domain socket or local TCP port."""
        sock_path = self.config.ipc_socket_path

        # Clean existing stale socket
        if os.path.exists(sock_path):
            try:
                os.unlink(sock_path)
            except OSError:
                pass

        try:
            server = await asyncio.start_unix_server(self.handle_client, path=sock_path)
            print(f"[VidMate AI Agent] Listening on Unix socket: {sock_path}", flush=True)
        except Exception:
            # Fallback to local TCP server
            port = self.config.ipc_port
            server = await asyncio.start_server(self.handle_client, host="127.0.0.1", port=port)
            print(f"[VidMate AI Agent] Listening on TCP: 127.0.0.1:{port}", flush=True)

        async with server:
            await server.serve_forever()


def main() -> None:
    """CLI entrypoint for agent daemon."""
    config = AgentConfig.load()
    service = VidMateAgentService(config)
    try:
        asyncio.run(service.run_socket_server())
    except KeyboardInterrupt:
        print("[VidMate AI Agent] Stopped by user.", flush=True)


if __name__ == "__main__":
    main()
