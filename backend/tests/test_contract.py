import unittest

from backend.main import (
    Action,
    Emotion,
    MemoryKind,
    MemoryUpdate,
    allowed_personality,
    make_system_prompt,
    safe_memory_update,
    validate_model_result,
)


class CompanionBackendContractTests(unittest.TestCase):
    def test_personality_allowlist_discards_unknown_fields(self):
        result = allowed_personality({
            "temperament": "energetic",
            "initiative": "high",
            "api_key": "must-not-pass",
            "private_note": "must-not-pass",
        })
        self.assertEqual(result, {"temperament": "energetic", "initiative": "high"})

    def test_prompt_is_nonverbal_friend_first_and_memory_limited(self):
        prompt = make_system_prompt("Milo", {"temperament": "calm"}, [])
        self.assertIn("digital friend", prompt)
        self.assertIn("Never generate speech", prompt)
        self.assertIn("sensitive facts", prompt)
        self.assertNotIn("virtual pet", prompt)

    def test_unaddressed_speech_cannot_write_memory(self):
        payload = {
            "addressed_to_companion": False,
            "understood": False,
            "intent": "ambient",
            "action": Action.idle,
            "emotion": Emotion.neutral,
            "confidence": 0.99,
            "nonverbal_cue": "remain calm",
            "clarification": None,
            "memory_updates": [
                MemoryUpdate(kind=MemoryKind.nickname, value="Milo", confidence=0.99)
            ],
        }
        with self.assertRaises(ValueError):
            validate_model_result(payload)

    def test_sensitive_memory_values_are_rejected_server_side(self):
        self.assertFalse(safe_memory_update(MemoryUpdate(
            kind=MemoryKind.interaction_preference,
            value="У меня диабет",
            confidence=0.99,
        )))
        self.assertFalse(safe_memory_update(MemoryUpdate(
            kind=MemoryKind.nickname,
            value="мой телефон +7 999 123-45-67",
            confidence=0.99,
        )))
        self.assertTrue(safe_memory_update(MemoryUpdate(
            kind=MemoryKind.favorite_activity,
            value="любит короткие прогулки",
            confidence=0.99,
        )))

    def test_model_contract_accepts_known_nonverbal_action(self):
        result = validate_model_result({
            "addressed_to_companion": True,
            "understood": True,
            "intent": "greeting",
            "action": "greet",
            "emotion": "happy",
            "confidence": 0.95,
            "nonverbal_cue": "brighten eyes and blink",
            "clarification": None,
            "memory_updates": [],
        })
        self.assertEqual(result.action, Action.greet)
        self.assertEqual(result.emotion, Emotion.happy)


if __name__ == "__main__":
    unittest.main()
