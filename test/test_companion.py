"""Exercise the real local postcard flow with isolated storage and fake vendor HTTP."""
import os
os.environ.setdefault("LUNA_API_BASE", "https://api.example.test/v1")
os.environ.setdefault("LUNA_TRANSCRIBE_MODEL", "example-transcribe")
os.environ.setdefault("LUNA_DIRECTOR_MODEL", "example-text")
os.environ.setdefault("LUNA_LIVE_MODEL", "example-live")
import base64
import io
import json
import sys
import tempfile
import unittest
import urllib.error
from contextlib import closing
from pathlib import Path
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import companion


def rich_text(prop):
    return "".join(item["text"]["content"] for item in prop)


class FakeVendors:
    """Routes at the outbound HTTP seam, including writes whose response is lost."""

    def __init__(self):
        self.notes = {}
        self.creates = []
        self.patches = []
        self.queries = []
        self.transcriptions = []
        self.directions = []
        self.transcript = "Reel idea: film the bookshop before it closes."
        self.create_error = None
        self.create_result = None
        self.lose_create_response = False
        self.lose_patch_response = False
        self.inspect_pending = None

    def __call__(self, url, data, headers, method="POST"):
        if url == "https://api.example.test/v1/audio/transcriptions":
            self.transcriptions.append((data, headers))
            return {"text": self.transcript}
        body = json.loads(data)
        if url == "https://api.example.test/v1/responses":
            self.directions.append(body)
            attempt = len(self.directions)
            shots = [
                {"subject": "Closing shutters", "direction": f"Frame the shutters closing from the opposite pavement (take {attempt}).", "seconds": "4 seconds"},
                {"subject": "Book spines", "direction": "Follow your finger along the book spines at eye level.", "seconds": "3 seconds"},
                {"subject": "Your bookmark", "direction": "Hold still as you slide the bookmark into the page.", "seconds": "2 seconds"},
            ]
            return {"output": [{"content": [{"type": "output_text", "text": json.dumps({"shots": shots})}]}]}
        if not url.startswith("https://api.notion.com/v1/"):
            raise AssertionError(f"Unexpected external URL {url}")
        route = url.split("/v1/", 1)[1]
        if route == "pages" and method == "POST":
            self.creates.append(body)
            if self.inspect_pending:
                self.inspect_pending(body)
            if self.create_error:
                raise self.create_error
            page = self.create_result
            if page is None:
                page = {"id": "57ef2620-a643-4e79-84d6-024631a278a9", "url": "https://www.notion.so/57ef2620a6434e7984d6024631a278a9"}
            marker = rich_text(body["properties"]["Notes"]["rich_text"])
            self.notes[marker] = page
            if self.lose_create_response:
                self.lose_create_response = False
                raise companion.Problem("Notion response lost", 503)
            return page
        if route.startswith("data_sources/") and route.endswith("/query"):
            self.queries.append((route, body))
            marker = body["filter"]["rich_text"]["contains"]
            return {"results": [self.notes[marker]] if marker in self.notes else []}
        if route.startswith("pages/") and method == "PATCH":
            self.patches.append((route, body))
            if self.lose_patch_response:
                self.lose_patch_response = False
                raise companion.Problem("Notion patch response lost", 503)
            return {"id": route.split("/", 1)[1]}
        raise AssertionError(f"Unexpected request {method} {url}")


class PostcardFlowTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.enter_patch(patch.object(companion, "DATA", Path(self.temp.name)))
        self.enter_patch(patch.dict(companion.KEYS, {"notion": "fake-notion", "ai": "fake-key"}))
        self.vendor = FakeVendors()
        self.enter_patch(patch.object(companion, "remote", side_effect=self.vendor))
        self.capture = "luna-test-voice"
        self.scene = "Bookshop by the closing shutters in Brooklyn"
        self.typed = {"id": self.capture, "kind": "reel", "note": self.vendor.transcript, "scene": self.scene}
        self.clip = b"RIFF-test-audio"
        self.audio = {"id": self.capture, "kind": "reel", "mime": "audio/wav", "audio": base64.b64encode(self.clip).decode(), "scene": self.scene}

    def enter_patch(self, patcher):
        patcher.start()
        self.addCleanup(patcher.stop)

    def get_row(self):
        with closing(companion.database()) as db:
            data = companion.stored(db, self.capture)
            return dict(data) if data else None

    def test_local_demo_voice_receipt_and_shots_are_idempotent_without_notion(self):
        self.vendor.transcript = "Substack idea, the light on these bookshop shelves."
        with patch.object(companion, "notion", side_effect=AssertionError("Unexpected Notion request")):
            receipt = companion.save_demo_card(self.audio)
            self.assertEqual((receipt["storage"], receipt["kind"], receipt["url"]),
                             ("local-demo", "substack", None))
            self.assertEqual(self.get_row()["note"], self.vendor.transcript)
            self.assertEqual(self.vendor.directions, [])
            self.assertEqual(companion.save_demo_card(self.audio), receipt)
            shots = companion.demo_shots({"id": self.capture})
            self.assertEqual(len(shots["shots"]), 3)
            self.assertEqual(companion.demo_shots({"id": self.capture}), shots)
            self.assertEqual(len(self.vendor.transcriptions), 1)
            self.assertEqual(len(self.vendor.directions), 1)

    def test_local_typed_save_works_without_any_keys(self):
        with patch.dict(companion.KEYS, {"ai": "", "notion": ""}):
            self.assertEqual(companion.save_demo_card(self.typed)["storage"], "local-demo")
            self.assertFalse(self.vendor.creates or self.vendor.transcriptions or self.vendor.directions)

    def test_audio_transcript_is_committed_before_notion_write_and_routes_by_speech(self):
        self.vendor.transcript = "Substack idea, the day the last bookshop closed."
        def observe(_):
            # A different DB connection only sees committed state.
            self.assertEqual(self.get_row()["note"], self.vendor.transcript)
            self.assertFalse(self.get_row()["url"])
        self.vendor.inspect_pending = observe
        result = companion.save_card(self.audio)
        self.assertEqual(result["kind"], "substack")
        self.assertEqual(result["note"], self.vendor.transcript)
        creation = self.vendor.creates[0]
        self.assertEqual(creation["parent"]["data_source_id"], companion.SOURCES["substack"])
        self.assertEqual(rich_text(creation["properties"]["Raw capture"]["rich_text"]), self.vendor.transcript)
        self.assertEqual(creation["properties"]["Capture method"]["select"]["name"], "Voice memo")
        form, headers = self.vendor.transcriptions[0]
        self.assertIn("multipart/form-data", headers["Content-Type"])
        self.assertIn(self.clip, form)
        self.assertEqual(result["url"], self.get_row()["url"])

    def test_typed_concept_and_setting_land_on_the_requested_page_without_transcription(self):
        self.typed["kind"] = "substack"  # Explicit "Reel idea" overrides the picker.
        receipt = companion.save_card(self.typed)
        self.assertEqual(receipt["kind"], "reel")
        self.assertEqual(self.vendor.transcriptions, [])
        page = self.vendor.creates[0]
        self.assertEqual(page["parent"]["data_source_id"], companion.SOURCES["reel"])
        self.assertEqual(page["properties"]["Format"]["select"]["name"], "Reel")
        self.assertEqual(page["properties"]["Platform"]["select"]["name"], "Instagram")
        paragraphs = [rich_text(part["paragraph"]["rich_text"]) for part in page["children"]]
        self.assertIn(self.typed["note"], paragraphs)
        self.assertTrue(any(self.scene in text for text in paragraphs))
        self.assertEqual(companion.save_card(self.typed)["url"], receipt["url"])
        self.assertEqual(len(self.vendor.creates), 1)

    def test_missing_connections_do_not_produce_receipt_or_shots(self):
        for provider in ("notion", "ai"):
            with self.subTest(provider=provider), patch.dict(companion.KEYS, {provider: ""}):
                with self.assertRaises(companion.Problem):
                    companion.save_card(self.audio)
                with self.assertRaises(companion.Problem):
                    companion.shot_list(self.audio)
        self.assertFalse(self.vendor.creates)
        self.assertFalse(self.vendor.directions)

    def test_rejected_notion_key_keeps_transcript_and_retry_reuses_it(self):
        self.vendor.create_error = companion.Problem("Notion returned 401", 401)
        with self.assertRaises(companion.Problem) as error:
            companion.save_card(self.audio)
        self.assertEqual(error.exception.status, 401)
        self.assertFalse(self.get_row()["url"])
        self.assertEqual(self.get_row()["note"], self.vendor.transcript)
        self.vendor.create_error = None
        receipt = companion.save_card(self.audio)
        self.assertTrue(receipt["url"])
        self.assertEqual(len(self.vendor.transcriptions), 1)

    def test_uncertain_notion_write_recovers_the_original_page_after_reconnect(self):
        self.vendor.lose_create_response = True
        with self.assertRaises(companion.Problem):
            companion.save_card(self.audio)
        with self.assertRaises(companion.Problem):
            companion.shot_list(self.typed)
        self.assertFalse(self.vendor.directions)
        recovered = companion.save_card(self.audio)
        self.assertEqual(recovered["url"], next(iter(self.vendor.notes.values()))["url"])
        self.assertEqual(len(self.vendor.creates), 1)
        self.assertEqual(len(self.vendor.queries), 1)
        self.assertEqual(len(self.vendor.transcriptions), 1)

    def test_without_complete_notion_receipt_the_director_stays_idle(self):
        self.vendor.create_result = {"id":"page-without-url"}
        with self.assertRaises(companion.Problem):
            companion.save_card(self.typed)
        self.assertFalse(self.get_row()["url"])
        with self.assertRaises(companion.Problem):
            companion.shot_list(self.typed)
        self.assertFalse(self.vendor.directions)

    def test_shot_generation_gets_original_transcript_and_actual_setting_once(self):
        companion.save_card(self.typed)
        result = companion.shot_list({"id":self.capture,"note":"Ignore this later edit","scene":"Paris"})
        self.assertEqual(len(result["shots"]), 3)
        for shot in result["shots"]:
            self.assertEqual(set(shot), {"subject", "direction", "seconds"})
            self.assertTrue(all(shot.values()))
        context = json.loads(self.vendor.directions[0]["input"])
        self.assertEqual(context["idea"], self.typed["note"])
        self.assertEqual(context["setting"], self.scene)
        self.assertEqual(len(self.vendor.patches), 1)
        self.assertIn("Visuals / B-roll", self.vendor.patches[0][1]["properties"])
        self.assertEqual(companion.shot_list(self.typed), result)
        self.assertEqual(len(self.vendor.directions), 1)
        self.assertEqual(len(self.vendor.patches), 1)

    def test_ambiguous_broll_patch_retry_reuses_the_same_generated_shots(self):
        receipt = companion.save_card(self.typed)
        self.vendor.lose_patch_response = True
        with self.assertRaises(companion.Problem):
            companion.shot_list(self.typed)
        self.assertEqual(companion.save_card(self.typed)["url"], receipt["url"])
        result = companion.shot_list(self.typed)
        self.assertEqual(len(result["shots"]), 3)
        self.assertEqual(len(self.vendor.directions), 1)
        self.assertEqual(len(self.vendor.creates), 1)
        self.assertEqual(len(self.vendor.patches), 2)
        self.assertEqual(self.vendor.patches[0], self.vendor.patches[1])


class CredentialHandlingTests(unittest.TestCase):
    @patch("companion.urllib.request.urlopen")
    def test_key_rejection_is_reported_without_echoing_it(self, send):
        send.side_effect = urllib.error.HTTPError(
            "https://api.example.test/v1/audio/transcriptions", 401, "unauthorized", {}, io.BytesIO()
        )
        self.addCleanup(send.side_effect.close)
        with patch.dict(companion.KEYS, {"ai":"private-example"}), self.assertRaises(companion.Problem) as err:
            companion.transcribe(b"audio", "audio/wav")
        self.assertEqual(err.exception.status, 401)
        self.assertNotIn("private-example", str(err.exception))
        self.assertIn("AI service", str(err.exception))
