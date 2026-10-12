"""Configuration and environment loader for VidMate AI Agent."""

from __future__ import annotations

import os
from pathlib import Path
from typing import Optional
from pydantic import BaseModel, Field


def _find_env_file() -> Optional[Path]:
    """Search for .env.local or .env in project roots or user config."""
    # Check parent directories of this script
    curr = Path(__file__).resolve().parent
    for _ in range(6):
        for candidate in [".env.local", ".env"]:
            p = curr / candidate
            if p.is_file():
                return p
        if curr.parent == curr:
            break
        curr = curr.parent

    # Check current working directory
    for candidate in [".env.local", ".env"]:
        p = Path.cwd() / candidate
        if p.is_file():
            return p

    return None


def _load_env_file(path: Path) -> dict[str, str]:
    """Parse simple KEY=VALUE pairs from a file without polluting globals."""
    env_vars: dict[str, str] = {}
    try:
        with open(path, "r", encoding="utf-8") as f:
            for line in f:
                line = line.strip()
                if not line or line.startswith("#"):
                    continue
                if "=" in line:
                    key, val = line.split("=", 1)
                    key = key.strip()
                    val = val.strip().strip("'\"")
                    if key:
                        env_vars[key] = val
    except Exception:
        pass
    return env_vars


class AgentConfig(BaseModel):
    """Configuration settings for VidMate AI Agent."""

    # Provider Keys
    openrouter_api_key: str = Field(default="")
    groq_api_key: str = Field(default="")
    custom_api_key: str = Field(default="")

    @property
    def groq_key(self) -> str:
        return self.groq_api_key

    @property
    def openrouter_key(self) -> str:
        return self.openrouter_api_key

    # Base URLs
    openrouter_base_url: str = Field(default="https://openrouter.ai/api/v1")
    groq_base_url: str = Field(default="https://api.groq.com/openai/v1")
    custom_base_url: str = Field(default="http://127.0.0.1:11434/v1")

    # Models
    openrouter_model: str = Field(default="anthropic/claude-3.5-sonnet")
    groq_model: str = Field(default="llama-3.3-70b-versatile")
    custom_model: str = Field(default="qwen2.5:latest")

    # Routing
    active_provider: str = Field(default="auto")  # "auto", "groq", "openrouter", "custom"

    # IPC
    ipc_socket_path: str = Field(default="/tmp/kdenlive_agent.sock")
    ipc_port: int = Field(default=8765)

    @classmethod
    def load(cls) -> AgentConfig:
        """Load configuration from environment and env files."""
        merged: dict[str, str] = {}

        # 1. Load from .env.local / .env if present
        env_path = _find_env_file()
        if env_path:
            merged.update(_load_env_file(env_path))

        # 2. System environment variables override file settings
        merged.update({k: v for k, v in os.environ.items() if v})

        # Resolve OpenRouter key
        or_key = (
            merged.get("OPENROUTER_API_KEY")
            or merged.get("LLM_OPENROUTER_API_KEY")
            or merged.get("x-openrouter-key")
            or ""
        )

        # Resolve Groq key
        groq_key = (
            merged.get("GROQ_API_KEY")
            or merged.get("LLM_GROQ_API_KEY")
            or merged.get("x-groq-key")
            or ""
        )

        # Resolve custom key
        custom_key = merged.get("LLM_OPENAI_API_KEY") or ""

        # Base URLs
        or_base = merged.get("OPENROUTER_BASE_URL", "https://openrouter.ai/api/v1")
        groq_base = merged.get("GROQ_BASE_URL", "https://api.groq.com/openai/v1")
        custom_base = merged.get("LLM_OPENAI_BASE_URL", "http://127.0.0.1:11434/v1")

        # Models
        or_model = merged.get("OPENROUTER_MODEL") or merged.get("LLM_OPENROUTER_MODEL") or "anthropic/claude-3.5-sonnet"
        groq_model = merged.get("GROQ_MODEL") or merged.get("LLM_GROQ_MODEL") or "llama-3.3-70b-versatile"
        custom_model = merged.get("LLM_OPENAI_MODEL") or "qwen2.5:latest"

        # Determine active provider
        provider = merged.get("LLM_PROVIDER", "auto").lower()
        if provider not in ("groq", "openrouter", "custom"):
            if groq_key:
                provider = "groq"
            elif or_key:
                provider = "openrouter"
            else:
                provider = "auto"

        return cls(
            openrouter_api_key=or_key,
            groq_api_key=groq_key,
            custom_api_key=custom_key,
            openrouter_base_url=or_base,
            groq_base_url=groq_base,
            custom_base_url=custom_base,
            openrouter_model=or_model,
            groq_model=groq_model,
            custom_model=custom_model,
            active_provider=provider,
            ipc_socket_path=merged.get("KDENLIVE_AGENT_SOCKET", "/tmp/kdenlive_agent.sock"),
            ipc_port=int(merged.get("KDENLIVE_AGENT_PORT", "8765")),
        )
