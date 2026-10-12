"""Provider and model factory for PydanticAI supporting OpenRouter and Groq."""

from __future__ import annotations

from typing import Optional
from pydantic_ai.models.openai import OpenAIChatModel
from pydantic_ai.providers.openrouter import OpenRouterProvider
from pydantic_ai.providers.groq import GroqProvider

from .config import AgentConfig


def create_agent_model(
    config: AgentConfig,
    provider: Optional[str] = None,
    model_name: Optional[str] = None,
) -> OpenAIChatModel:
    """Instantiate a PydanticAI model configured for OpenRouter, Groq, or custom."""
    chosen_provider = (provider or config.active_provider).lower()

    # Automatic selection based on available keys if "auto"
    if chosen_provider == "auto":
        if config.groq_key:
            chosen_provider = "groq"
        elif config.openrouter_api_key:
            chosen_provider = "openrouter"
        elif config.supabase_proxy_url and config.supabase_anon_key:
            chosen_provider = "supabase_proxy"
        elif config.custom_api_key:
            chosen_provider = "custom"
        else:
            # Default to Groq if keys will be provided per-request
            chosen_provider = "groq"

    if chosen_provider == "groq":
        target_model = model_name or config.groq_model
        p = GroqProvider(
            api_key=config.groq_key or "missing-groq-key",
            base_url=config.groq_base_url,
        )
        return OpenAIChatModel(target_model, provider=p)

    if chosen_provider == "openrouter":
        target_model = model_name or config.openrouter_model
        p = OpenRouterProvider(
            api_key=config.openrouter_api_key or "missing-openrouter-key",
            app_url="https://vidmate.ai",
            app_title="VidMate AI-Mate",
        )
        return OpenAIChatModel(target_model, provider=p)

    if chosen_provider in ("supabase_proxy", "ai-proxy", "supabase"):
        target_model = model_name or "auto"
        from openai import AsyncOpenAI
        from pydantic_ai.providers.openai import OpenAIProvider

        headers = {
            "apikey": config.supabase_anon_key or "missing-anon-key",
            "Authorization": f"Bearer {config.supabase_anon_key or 'missing-anon-key'}",
        }
        if config.openrouter_api_key:
            headers["x-openrouter-key"] = config.openrouter_api_key
        if config.groq_key:
            headers["x-groq-key"] = config.groq_key

        client = AsyncOpenAI(
            base_url=config.supabase_proxy_url or "http://localhost:54321/functions/v1/ai-proxy",
            api_key=config.supabase_anon_key or "dummy-key",
            default_headers=headers,
        )
        p = OpenAIProvider(openai_client=client)
        return OpenAIChatModel(target_model, provider=p)

    # Custom OpenAI-compatible fallback (e.g. Ollama, LM Studio, vLLM)
    target_model = model_name or config.custom_model
    from pydantic_ai.providers.openai import OpenAIProvider
    p = OpenAIProvider(
        api_key=config.custom_api_key or "dummy-key",
        base_url=config.custom_base_url,
    )
    return OpenAIChatModel(target_model, provider=p)
