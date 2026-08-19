import os
from pathlib import Path

import requests
from dotenv import load_dotenv
from flask import Flask, jsonify, request, send_file

load_dotenv()

BASE_DIR = Path(__file__).resolve().parent
GENERATED_DIR = BASE_DIR / "generated"
GENERATED_DIR.mkdir(exist_ok=True)
LATEST_PCM = GENERATED_DIR / "latest.pcm"

ELEVENLABS_API_KEY = os.getenv("ELEVENLABS_API_KEY", "").strip()
HOST = os.getenv("HOST", "0.0.0.0")
PORT = int(os.getenv("PORT", "5001"))

ELEVENLABS_BASE = "https://api.elevenlabs.io/v1"
TTS_MODEL_ID = "eleven_multilingual_v2"
OUTPUT_FORMAT = "pcm_16000"  # raw 16-bit PCM, 16 kHz; convenient for ESP32 I2S

app = Flask(__name__)


def require_api_key():
    if not ELEVENLABS_API_KEY or ELEVENLABS_API_KEY == "YOUR_API_KEY":
        return jsonify(
            {
                "error": "ELEVENLABS_API_KEY is not configured.",
                "hint": "Copy .env.example to .env and add your own key locally.",
            }
        ), 503
    return None


def eleven_headers():
    return {"xi-api-key": ELEVENLABS_API_KEY}


@app.get("/health")
def health():
    return jsonify(
        {
            "status": "ok",
            "test": "B — Voice Pipeline",
            "output_format": OUTPUT_FORMAT,
            "sample_rate_hz": 16000,
            "channels": 1,
            "sample_width_bits": 16,
        }
    )


@app.post("/clone")
def clone_voice():
    """
    Create an Instant Voice Clone from one explicitly consented sample.

    Multipart form fields:
      sample              audio file
      voice_name          optional display name
      consent_confirmed   must be true / 1 / yes
    """
    key_error = require_api_key()
    if key_error:
        return key_error

    consent = request.form.get("consent_confirmed", "").strip().lower()
    if consent not in {"true", "1", "yes"}:
        return jsonify(
            {
                "error": "Explicit consent confirmation is required.",
                "required_field": "consent_confirmed=true",
            }
        ), 400

    if "sample" not in request.files:
        return jsonify({"error": "Missing multipart file field: sample"}), 400

    sample = request.files["sample"]
    if not sample.filename:
        return jsonify({"error": "The uploaded sample has no filename."}), 400

    voice_name = request.form.get("voice_name", "Love Voice Family Test").strip()
    if not voice_name:
        voice_name = "Love Voice Family Test"

    # Read into memory so the request sent upstream is independent of Flask's temp stream.
    sample_bytes = sample.read()
    if not sample_bytes:
        return jsonify({"error": "The uploaded sample is empty."}), 400

    files = [
        (
            "files[]",
            (
                sample.filename,
                sample_bytes,
                sample.mimetype or "application/octet-stream",
            ),
        )
    ]

    form = {
        "name": voice_name,
        "description": "Love Voice prototype — consented family voice test",
        "remove_background_noise": "false",
    }

    try:
        response = requests.post(
            f"{ELEVENLABS_BASE}/voices/add",
            headers=eleven_headers(),
            data=form,
            files=files,
            timeout=120,
        )
    except requests.RequestException as exc:
        return jsonify({"error": "Voice-clone request failed", "detail": str(exc)}), 502

    if not response.ok:
        return jsonify(
            {
                "error": "Voice-clone API returned an error",
                "status_code": response.status_code,
                "detail": response.text[:2000],
            }
        ), 502

    data = response.json()
    return jsonify(
        {
            "status": "created",
            "voice_id": data.get("voice_id"),
            "requires_verification": data.get("requires_verification"),
            "consent_confirmed": True,
            "next": "POST /tts with this voice_id and test text.",
        }
    )


@app.post("/tts")
def generate_tts():
    """
    Generate raw 16 kHz PCM for ESP32 playback.

    JSON body:
      {
        "voice_id": "...",
        "text": "Mom, remember to take your medicine today."
      }
    """
    key_error = require_api_key()
    if key_error:
        return key_error

    body = request.get_json(silent=True) or {}
    voice_id = str(body.get("voice_id", "")).strip()
    text = str(body.get("text", "")).strip()

    if not voice_id:
        return jsonify({"error": "voice_id is required"}), 400
    if not text:
        return jsonify({"error": "text is required"}), 400
    if len(text) > 1000:
        return jsonify({"error": "For this prototype, keep text at 1000 characters or fewer."}), 400

    url = f"{ELEVENLABS_BASE}/text-to-speech/{voice_id}"
    params = {"output_format": OUTPUT_FORMAT}
    payload = {
        "text": text,
        "model_id": TTS_MODEL_ID,
    }
    headers = {
        **eleven_headers(),
        "Content-Type": "application/json",
        "Accept": "application/octet-stream",
    }

    try:
        response = requests.post(
            url,
            params=params,
            headers=headers,
            json=payload,
            timeout=120,
        )
    except requests.RequestException as exc:
        return jsonify({"error": "TTS request failed", "detail": str(exc)}), 502

    if not response.ok:
        return jsonify(
            {
                "error": "TTS API returned an error",
                "status_code": response.status_code,
                "detail": response.text[:2000],
            }
        ), 502

    LATEST_PCM.write_bytes(response.content)

    return jsonify(
        {
            "status": "generated",
            "bytes": len(response.content),
            "format": OUTPUT_FORMAT,
            "sample_rate_hz": 16000,
            "channels": 1,
            "sample_width_bits": 16,
            "audio_path": "/audio/latest.pcm",
            "note": "Label this playback as AI-generated familiar speech during user testing.",
        }
    )


@app.get("/audio/latest.pcm")
def latest_audio():
    if not LATEST_PCM.exists():
        return jsonify(
            {
                "error": "No generated audio yet.",
                "hint": "POST /tts first.",
            }
        ), 404

    return send_file(
        LATEST_PCM,
        mimetype="application/octet-stream",
        as_attachment=False,
        download_name="latest.pcm",
        conditional=False,
        max_age=0,
    )


if __name__ == "__main__":
    print("Love Voice — B Test: Voice Pipeline")
    print(f"Local server: http://127.0.0.1:{PORT}")
    print("Use only explicitly consented voice samples.")
    app.run(host=HOST, port=PORT, debug=False)
