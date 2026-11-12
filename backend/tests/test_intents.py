import unittest

from pydantic import ValidationError

from backend.main import (
    Action,
    Emotion,
    InterpretRequest,
    MemoryUpdate,
    allowed_personality,
    make_system_prompt,
    validate_model_result,
)


def valid_result(**overrides):
    result = {
        "addressed_to_companion": True,
        "understood": True,
        "intent": "owner_invites_play",
        "action": "play",
        "emotion": "happy",
        "confidence": 0.93,
        "nonverbal_cue": "Поднять глаза и слегка наклониться вперёд",
        "clarification": None,
        "memory_updates": [],
    }
    result.update(overrides)
    return result


class IntentValidationTests(unittest.TestCase):
    def test_accepts_known_intent(self):
        result = validate_model_result(valid_result())
        self.assertEqual(result.action, Action.play)
        self.assertEqual(result.emotion, Emotion.happy)
        self.assertTrue(result.addressed_to_companion)

    def test_accepts_non_addressed_utterance_as_idle(self):
        result = validate_model_result(valid_result(
            addressed_to_companion=False, understood=False, action="idle",
            emotion="neutral", intent="not_addressed", confidence=0.98,
            nonverbal_cue="Спокойно продолжать наблюдать",
        ))
        self.assertFalse(result.addressed_to_companion)
        self.assertEqual(result.action, Action.idle)

    def test_rejects_unknown_action(self):
        with self.assertRaises(ValidationError):
            validate_model_result(valid_result(action="execute_shell"))

    def test_rejects_unknown_emotion(self):
        with self.assertRaises(ValidationError):
            validate_model_result(valid_result(emotion="rage_mode"))

    def test_rejects_out_of_range_confidence(self):
        with self.assertRaises(ValidationError):
            validate_model_result(valid_result(confidence=4))

    def test_requires_address_detection_field(self):
        result = valid_result()
        result.pop("addressed_to_companion")
        with self.assertRaises(ValidationError):
            validate_model_result(result)


    def test_accepts_safe_durable_memory_update(self):
        result = validate_model_result(valid_result(memory_updates=[{
            "kind": "favorite_activity", "value": "любит спокойные игры", "confidence": 0.91,
        }]))
        self.assertEqual(result.memory_updates[0].kind.value, "favorite_activity")

    def test_rejects_unknown_memory_category(self):
        with self.assertRaises(ValidationError):
            validate_model_result(valid_result(memory_updates=[{
                "kind": "medical_condition", "value": "private fact", "confidence": 0.99,
            }]))

    def test_rejects_memory_update_for_unaddressed_speech(self):
        with self.assertRaises(ValueError):
            validate_model_result(valid_result(
                addressed_to_companion=False, understood=False, action="idle",
                emotion="neutral", memory_updates=[{
                    "kind": "nickname", "value": "Milo", "confidence": 0.99,
                }],
            ))

    def test_existing_memory_facts_are_available_as_context(self):
        fact = MemoryUpdate(kind="favorite_activity", value="спокойные игры", confidence=0.95)
        prompt = make_system_prompt("Milo", {}, [fact])
        self.assertIn("спокойные игры", prompt)

    def test_limits_context_and_normalizes_utterance(self):
        payload = InterpretRequest(utterance="  привет   малыш  ")
        self.assertEqual(payload.utterance, "привет малыш")

    def test_rejects_too_many_context_turns(self):
        with self.assertRaises(ValidationError):
            InterpretRequest(utterance="Привет", recent_context=[{"role": "owner", "text": "x"}] * 9)

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
        self.assertIn("addressed_to_companion", prompt)


if __name__ == "__main__":
    unittest.main()
