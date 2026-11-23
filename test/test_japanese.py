"""Offline checks for the local live-voice signalling adapter."""
import os
os.environ.setdefault("LUNA_API_BASE", "https://api.example.test/v1")
os.environ.setdefault("LUNA_TRANSCRIBE_MODEL", "example-transcribe")
os.environ.setdefault("LUNA_DIRECTOR_MODEL", "example-text")
os.environ.setdefault("LUNA_LIVE_MODEL", "example-live")
import io
import json
import sys
import unittest
import urllib.error
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from japanese_session import start_session


class StartSessionTests(unittest.TestCase):
    @patch("japanese_session.urllib.request.urlopen")
    def test_session_has_lesson_context_and_server_only_credentials(self, send):
        send.return_value = io.BytesIO(b'{"transport":{"type":"webrtc","sdp":"v=0 answer"}}')
        self.assertEqual(start_session("test-key", "v=0 offer"), "v=0 answer")
        req = send.call_args.args[0]
        self.assertEqual(req.full_url, "https://api.example.test/v1/live/sessions")
        self.assertEqual(req.headers["Authorization"], "Bearer test-key")
        body = json.loads(req.data)
        self.assertEqual(body["transport"], {"type": "webrtc", "sdp": "v=0 offer"})
        self.assertIn("Lesson 5", body["session"]["instructions"])
        self.assertNotIn("Lesson 9", body["session"]["instructions"])
        self.assertFalse(body["session"]["store"])
        self.assertNotIn("test-key", req.data.decode())

    @patch("japanese_session.urllib.request.urlopen")
    def test_invalid_offer_or_key_never_connects(self, send):
        for key, offer in [("", "v=0 offer"), ("test-key", "invalid"), ("test-key", "v=0" + "a" * 66000)]:
            with self.assertRaises(ValueError):
                start_session(key, offer)
        send.assert_not_called()

    @patch("japanese_session.urllib.request.urlopen")
    def test_provider_failure_has_user_action(self, send):
        send.side_effect = urllib.error.HTTPError("https://example.test", 403, "", {}, None)
        with self.assertRaisesRegex(RuntimeError, "service key"):
            start_session("test-key", "v=0 offer")

    @patch("japanese_session.urllib.request.urlopen")
    def test_bad_transport_is_rejected(self, send):
        send.return_value = io.BytesIO(b'{"transport":{"type":"websocket","sdp":""}}')
        with self.assertRaisesRegex(RuntimeError, "incomplete"):
            start_session("test-key", "v=0 offer")
