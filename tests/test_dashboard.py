import math
import unittest

import dashboard


class DashboardTelemetryTests(unittest.TestCase):
    def test_first_fix_is_launch_site_north_of_turret(self):
        telemetry = dashboard.telemetry_at(0.0, 0)

        self.assertEqual(telemetry["rocket_east_m"], 0.0)
        self.assertEqual(telemetry["rocket_north_m"], 100.0)
        self.assertEqual(telemetry["rocket_up_m"], 0.0)
        self.assertEqual(telemetry["distance_m"], 100.0)
        self.assertEqual(telemetry["yaw_deg"], 0.0)
        self.assertEqual(telemetry["tilt_deg"], 0.0)

    def test_coordinates_share_the_configured_ground_station_origin(self):
        telemetry = dashboard.telemetry_at(0.0, 0)
        expected_latitude = dashboard.GROUND_STATION_LATITUDE_DEG + math.degrees(
            100.0 / dashboard.EARTH_RADIUS_M
        )

        self.assertAlmostEqual(
            telemetry["rocket_latitude_deg"], expected_latitude, places=7
        )
        self.assertEqual(
            telemetry["rocket_longitude_deg"],
            dashboard.GROUND_STATION_LONGITUDE_DEG,
        )
        self.assertEqual(
            telemetry["rocket_altitude_asl_m"],
            dashboard.GROUND_STATION_ALTITUDE_ASL_M,
        )

    def test_position_and_turret_geometry_agree(self):
        telemetry = dashboard.telemetry_at(16.0, 16)
        east_m = telemetry["rocket_east_m"]
        north_m = telemetry["rocket_north_m"]
        up_m = telemetry["rocket_up_m"]
        expected_distance = math.hypot(east_m, north_m)
        expected_yaw = math.degrees(math.atan2(east_m, north_m))
        expected_tilt = math.degrees(math.atan2(up_m, expected_distance))

        self.assertAlmostEqual(telemetry["distance_m"], expected_distance, places=1)
        self.assertAlmostEqual(telemetry["yaw_deg"], expected_yaw, places=1)
        self.assertAlmostEqual(telemetry["tilt_deg"], expected_tilt, places=1)

    def test_coordinate_contract_is_present(self):
        telemetry = dashboard.telemetry_at(40.5, 41)
        fields = {
            "rocket_latitude_deg",
            "rocket_longitude_deg",
            "rocket_altitude_asl_m",
            "rocket_east_m",
            "rocket_north_m",
            "rocket_up_m",
            "ground_station_latitude_deg",
            "ground_station_longitude_deg",
            "ground_station_altitude_asl_m",
        }

        self.assertTrue(fields.issubset(telemetry))
        self.assertTrue(all(math.isfinite(telemetry[field]) for field in fields))


if __name__ == "__main__":
    unittest.main()
