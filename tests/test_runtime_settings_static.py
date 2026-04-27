from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_advanced_page_exposes_runtime_switches():
    html = read("local_components/esp-wifi-connect/assets/wifi_configuration.html")

    assert "auto_firmware_upgrade" in html
    assert "wake_arbitration_enabled" in html


def test_advanced_config_persists_runtime_switches():
    source = read("local_components/esp-wifi-connect/wifi_configuration_ap.cc")
    header = read("local_components/esp-wifi-connect/include/wifi_configuration_ap.h")

    assert "auto_firmware_upgrade_" in header
    assert "wake_arbitration_enabled_" in header
    assert '"auto_firmware_upgrade"' in source
    assert '"wake_arbitration_enabled"' in source
    assert "nvs_set_u8(nvs, \"auto_firmware_upgrade\"" in source
    assert "nvs_set_u8(nvs, \"wake_arbitration_enabled\"" in source


def test_application_uses_runtime_switches():
    source = read("main/application.cc")
    header = read("main/application.h")

    assert "IsAutoFirmwareUpgradeEnabled" in source
    assert "IsWakeArbitrationEnabled" in source
    assert "wake_arbitration_session_active_" in header
    assert "if (ota_->HasNewVersion() && IsAutoFirmwareUpgradeEnabled())" in source
    assert "if (!IsWakeArbitrationEnabled())" in source


def test_wake_arbitration_timeout_and_cost_logging_are_configured():
    source = read("main/wake_arbiter_client.cc")

    assert "kWakeArbitrationTimeoutMs = 800" in source
    assert "http->SetTimeout(kWakeArbitrationTimeoutMs)" in source
    assert "Wake arbitration cost:" in source
