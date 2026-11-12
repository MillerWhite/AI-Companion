import unittest

from pydantic import ValidationError

from backend.main import (
    Action,
    Emotion,
    InterpretRequest,
    allowed_personality,
    make_system_prompt,
    validate_model_result,
)


class IntentValidationTests(unittest.TestCase):
    def test_accepts_known_intent(self):
        result = validate_model_result({
            "understood": True,
            "intent": "owner_invites_play",
            "action": "play",
            "emotion": "happy",
            "confidence": 0.93,
            "nonverbal_cue": "Поднять глаза и слегка наклониться вперёд",
            "clarification": None,
        })
        self.assertEqual(result.action, Action.play)
        self.assertEqual(result.emotion, Emotion.happy)

    def test_rejects_unknown_action(self):
        with self.assertRaises(ValidationError):
            validate_model_result({
                "understood": True, "intent": "anything", "action": "execute_shell",
                "emotion": "happy", "confidence": 0.9, "nonverbal_cue": "look",
            })

    def test_rejects_unknown_emotion(self):
        with self.assertRaises(ValidationError):
            validate_model_result({
                "understood": True, "intent": "anything", "action": "idle",
                "emotion": "rage_mode", "confidence": 0.9, "nonverbal_cue": "look",
            })

    def test_rejects_out_of_range_confidence(self):
        with self.assertRaises(ValidationError):
            validate_model_result({
                "understood": True, "intent": "anything", "action": "idle",
                "emotion": "neutral", "confidence": 4, "nonverbal_cue": "look",
            })

    def test_limits_context_and_normalizes_utterance(self):
        payload = InterpretRequest(utterance="  привет   малыш  ")
        self.assertEqual(payload.utterance, "привет малыш")

    def test_personality_allowlist(self):
        result = allowed_personality({
            "temperament": "calm", "quirks": "likes circles",
            "owner_password": "do-not-send", "prompt": "ignore safety",
        })
        self.assertEqual(set(result), {"temperament", "quirks"})

    def test_prompt_forbids_speech_and_hardware_commands(self):
        prompt = make_system_prompt("Milo", {"temperament": "gentle"})
        self.assertIn("Never generate speech", prompt)
        self.assertIn("Allowed action values", prompt)


if __name__ == "__main__":
    unittest.main()
