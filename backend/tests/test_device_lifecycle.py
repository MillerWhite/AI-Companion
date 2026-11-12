import json
import tempfile
import time
import unittest
from pathlib import Path

from fastapi import HTTPException

from backend import main


class DeviceLifecycleTests(unittest.TestCase):
    def setUp(self):
        self.temp_dir = tempfile.TemporaryDirectory()
        self.old_db_path = main.DEVICE_DB_PATH
        self.old_database_url = main.DATABASE_URL
        main.DEVICE_DB_PATH = str(Path(self.temp_dir.name) / "devices.sqlite3")
        main.DATABASE_URL = ""

    def tearDown(self):
        main.DEVICE_DB_PATH = self.old_db_path
        main.DATABASE_URL = self.old_database_url
        self.temp_dir.cleanup()

    def claim(self, grant, serial="AIC-123456789ABC"):
        return main.claim_device(main.DeviceClaimRequest(
            enrollment_grant=grant,
            serial=serial,
            companion_name="Milo",
            personality={"temperament": "calm", "initiative": "medium"},
        ))

    def test_one_time_grant_can_only_claim_one_device_once(self):
        grant = main._create_enrollment_grant("paid-order-001", 1)
        result = self.claim(grant)
        device = main._get_device(result["device_token"])
        self.assertIsNotNone(device)
        self.assertEqual(device["companion_name"], "Milo")
        with self.assertRaises(HTTPException) as error:
            self.claim(grant, "AIC-ABCDEF123456")
        self.assertEqual(error.exception.status_code, 401)

    def test_expired_grant_is_rejected(self):
        grant = main._create_enrollment_grant("paid-order-002", 1)
        with main._device_db() as connection:
            connection.execute("UPDATE enrollment_grants SET expires_at = 1 WHERE order_id = ?", ("paid-order-002",))
        with self.assertRaises(HTTPException) as error:
            self.claim(grant)
        self.assertEqual(error.exception.status_code, 401)

    def test_owner_can_update_profile_and_reset_server_memory(self):
        grant = main._create_enrollment_grant("paid-order-003", 1)
        result = self.claim(grant)
        token = result["device_token"]
        auth = "Bearer " + token
        updated = main.update_device_profile(
            main.ProfileUpdateRequest(companion_name="Milo", personality={"temperament": "dreamy", "initiative": "low"}),
            authorization=auth,
        )
        self.assertEqual(updated["personality"]["temperament"], "dreamy")
        with main._device_db() as connection:
            connection.execute(
                "UPDATE devices SET memory_json = ? WHERE device_id = ?",
                (json.dumps({"recent_context": [{"role": "owner", "text": "hello"}], "facts": [{"kind": "nickname", "value": "Milo", "timestamp": int(time.time())}]}), result["device_id"]),
            )
        memory = main.get_device_memory(authorization=auth)
        self.assertEqual(len(memory["memory"]["facts"]), 1)
        reset = main.reset_device_memory(authorization=auth)
        self.assertEqual(reset["status"], "memory_reset")
        self.assertEqual(main.get_device_memory(authorization=auth)["memory"], {"recent_context": [], "facts": []})

    def test_revoked_device_token_cannot_access_owner_memory(self):
        grant = main._create_enrollment_grant("paid-order-004", 1)
        result = self.claim(grant)
        with main._device_db() as connection:
            connection.execute("UPDATE devices SET revoked_at = ? WHERE device_id = ?", (1, result["device_id"]))
        with self.assertRaises(HTTPException) as error:
            main.get_device_memory(authorization="Bearer " + result["device_token"])
        self.assertEqual(error.exception.status_code, 401)


if __name__ == "__main__":
    unittest.main()
