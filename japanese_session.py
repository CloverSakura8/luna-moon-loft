"""Live-voice signalling for Luna's Japanese practice on the local companion.

The provider is configured from the environment (see .env.example):
LUNA_API_BASE, LUNA_LIVE_MODEL, and optionally LUNA_LIVE_FALLBACK_MODEL,
LUNA_TUTOR_MODEL and LUNA_VOICE."""
import json
import os
import sys
import urllib.error
import urllib.request

TUTOR = (
    "You are Luna, Tanya's warm Japanese conversation partner. Speak at a beginner pace "
    "in short, natural Japanese. Practise Genki I Lesson 5: describing outings with "
    "い- and な-adjectives in present or past, 好き and きらい preferences, and suggestions "
    "with ～ましょう. Use original situations about food, travel, or an NYC walk. Start by "
    "asking which food she likes. Ask one question, wait, and make your next turn about "
    "what she actually said. Let her interrupt. If requested, repeat or slow down. "
    "Offer a short English hint only when requested or when she is stuck. "
    "After three exchanges, offer one specific correction or natural alternative and "
    "let her try it again. Do the tutoring directly; no external tools are needed."
)

# The first live-voice model that the account can use wins, so the demo still talks.
def configured_models():
    return [m for m in (os.environ.get("LUNA_LIVE_MODEL"), os.environ.get("LUNA_LIVE_FALLBACK_MODEL")) if m]
LAST_MODEL = None

def start_session(api_key, offer_sdp):
    """Try each Live model in order; the project key remains server-side."""
    global LAST_MODEL
    failure = None
    MODELS = configured_models()
    if not MODELS or not os.environ.get("LUNA_API_BASE"):
        raise RuntimeError("Set LUNA_API_BASE and LUNA_LIVE_MODEL for your voice provider. See the README.")
    for model in dict.fromkeys(MODELS):
        try:
            answer = _start(api_key, offer_sdp, model)
            LAST_MODEL = model
            print("Japanese voice session started with " + model, file=sys.stderr, flush=True)
            return answer
        except ModelUnavailable as exc:
            print("Live model " + model + " unavailable: " + str(exc), file=sys.stderr, flush=True)
            failure = exc
    raise RuntimeError("Check your AI service key and its live-voice access in Loft connections.") from failure

class ModelUnavailable(Exception):
    pass

def _start(api_key, offer_sdp, model):
    """Exchange a browser offer for an SDP answer; the project key remains server-side."""
    if not api_key:
        raise ValueError("Connect your AI service in the Loft to start a voice session.")
    if not isinstance(offer_sdp, str) or not offer_sdp.startswith("v=0") or len(offer_sdp) > 65536:
        raise ValueError("Invalid browser audio offer.")
    session = {
        "model": model,
        "instructions": TUTOR,
        "store": False,
        **({"audio": {"output": {"voice": os.environ["LUNA_VOICE"]}}} if os.environ.get("LUNA_VOICE") else {}),
        **({"delegation": {
            "type": "responses",
            "responses": {"model": os.environ["LUNA_TUTOR_MODEL"], "instructions": "Explain a beginner Japanese grammar point briefly when asked. Use the learner's actual example."}
        }} if os.environ.get("LUNA_TUTOR_MODEL") else {}),
        "client": {"data_channel": {
            "allowed_client_events": ["session.close", "session.commentary.append", "session.instructions.append"],
            "allowed_server_events": [{"type": name} for name in (
                "session.started", "session.closed", "session.output_transcript.delta", "error"
            )]
        }}
    }
    body = json.dumps({"session": session, "transport": {"type": "webrtc", "sdp": offer_sdp}}).encode()
    req = urllib.request.Request(os.environ.get("LUNA_API_BASE", "").rstrip("/") + "/live/sessions", body, {
        "Authorization": "Bearer " + api_key, "Content-Type": "application/json"
    }, method="POST")
    try:
        with urllib.request.urlopen(req, timeout=20) as response:
            result = json.load(response)
    except urllib.error.HTTPError as exc:
        detail = ""
        try: detail = exc.read(600).decode("utf-8", "replace")
        except OSError: pass
        if exc.code in (400, 403, 404) and "model" in detail.lower():
            raise ModelUnavailable(str(exc.code) + " " + detail[:300]) from None
        if exc.code in (401, 403, 404):
            raise RuntimeError("Check your AI service key and its live-voice access in Loft connections.") from None
        if exc.code == 429:
            raise RuntimeError("Your AI service reached its usage limit. Try again later.") from None
        raise RuntimeError("The AI service could not start the voice session. Try again.") from None
    except (OSError, ValueError):
        raise RuntimeError("The companion could not reach the live-voice service. Check the connection and try again.") from None
    answer = result.get("transport", {}) if isinstance(result, dict) else {}
    if not isinstance(answer, dict) or answer.get("type") != "webrtc" or not str(answer.get("sdp", "")).startswith("v=0"):
        raise RuntimeError("The live-voice service returned an incomplete audio connection.")
    return answer["sdp"]
