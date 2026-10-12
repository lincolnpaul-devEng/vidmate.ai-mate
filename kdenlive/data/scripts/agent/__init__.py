"""VidMate AI-Mate Agent Service package."""

from .config import AgentConfig
from .providers import create_agent_model
from .agent import KdenliveDeps, build_vidmate_agent
from .service import VidMateAgentService
from . import schemas

__all__ = [
    "AgentConfig",
    "create_agent_model",
    "KdenliveDeps",
    "build_vidmate_agent",
    "VidMateAgentService",
    "schemas",
]
