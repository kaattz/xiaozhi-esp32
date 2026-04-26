#ifndef WAKE_ARBITER_CLIENT_H
#define WAKE_ARBITER_CLIENT_H

#include <string>

enum class WakeArbitrationDecision {
    kAllowSession,
    kDenySession,
};

WakeArbitrationDecision ParseWakeArbitrationDecision(const std::string& response_body);

class WakeArbiterClient {
public:
    bool RequestSession(const std::string& wake_word);
    bool EndSession();

private:
    std::string BuildEndpointUrl(const std::string& path) const;
    std::string BuildWakeDetectedPayload(const std::string& wake_word) const;
    std::string BuildSessionEndPayload() const;
};

#endif // WAKE_ARBITER_CLIENT_H
