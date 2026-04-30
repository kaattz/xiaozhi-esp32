from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


def read(path: str) -> str:
    return (ROOT / path).read_text(encoding="utf-8")


def test_ha_mqtt_dependencies_are_declared():
    manifest = read("main/idf_component.yml")

    assert "johboh/homeassistantentities" in manifest
    assert "johboh/mqttremote" in manifest


def test_ha_mqtt_settings_wrapper_uses_nvs_namespace():
    header = read("main/ha_mqtt_settings.h")
    source = read("main/ha_mqtt_settings.cc")

    assert "class HaMqttSettings" in header
    assert 'Settings settings("ha_mqtt"' in source
    assert 'constexpr const char* kHaMqttNamespace = "ha_mqtt"' in source
    assert "bool enabled" in header
    assert "std::string host" in header
    assert "int port" in header
    assert "bool IsValid() const" in header
    assert "GenerateDefaultClientId" in source


def test_home_assistant_manager_exposes_expected_entities_and_callbacks():
    header = read("main/home_assistant_manager.h")
    source = read("main/home_assistant_manager.cc")

    for token in [
        "HaEntityButton",
        "HaEntityText",
        "HaEntityString",
        "HaEntityNumber",
        "PublishDeviceState",
        "PublishUserMessage",
        "PublishAssistantMessage",
        "PublishVolume",
        "PublishBrightness",
    ]:
        assert token in header

    assert "Application::GetInstance().WakeWordInvoke" in source
    assert "publishConfiguration" in source
    assert "publishString" in source
    assert "publishNumber" in source


def test_application_publishes_ha_state_and_conversation_text():
    source = read("main/application.cc")

    assert '#include "home_assistant_manager.h"' in source
    assert "HomeAssistantManager::GetInstance().Start()" in source
    assert "PublishDeviceState" in source
    assert "PublishUserMessage" in source
    assert "PublishAssistantMessage" in source


def test_wifi_config_page_and_backend_include_ha_mqtt_settings():
    html = read("local_components/esp-wifi-connect/assets/wifi_configuration.html")
    source = read("local_components/esp-wifi-connect/wifi_configuration_ap.cc")
    header = read("local_components/esp-wifi-connect/include/wifi_configuration_ap.h")

    assert "ha_mqtt" in html
    assert "ha_mqtt_enabled" in html
    assert "ha_mqtt_host" in html
    assert "ha_mqtt" in source
    assert "LoadHaMqttSettings" in source
    assert "SaveHaMqttSettings" in source
    assert "ha_mqtt_enabled_" in header


def test_send_wake_word_detected_uses_json_builder():
    source = read("main/protocols/protocol.cc")

    assert "cJSON_CreateObject" in source
    assert 'cJSON_AddStringToObject(root, "text", wake_word.c_str())' in source
    assert "cJSON_PrintUnformatted" in source

