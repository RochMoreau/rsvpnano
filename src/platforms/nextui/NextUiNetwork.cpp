#include "platforms/nextui/NextUiNetwork.h"

#include <arpa/inet.h>
#include <ifaddrs.h>
#include <net/if.h>
#include <netinet/in.h>

#include <string_view>

namespace NextUiNetwork {

    std::string address() {
        ifaddrs* interfaces = nullptr;
        if (getifaddrs(&interfaces) != 0)
            return {};
        std::string found;
        bool foundWireless = false;
        for (const ifaddrs* entry = interfaces; entry != nullptr; entry = entry->ifa_next) {
            if (entry->ifa_addr == nullptr || entry->ifa_addr->sa_family != AF_INET)
                continue;
            if ((entry->ifa_flags & IFF_UP) == 0 || (entry->ifa_flags & IFF_LOOPBACK) != 0)
                continue;
            char text[INET_ADDRSTRLEN]{};
            const auto* address = reinterpret_cast<const sockaddr_in*>(entry->ifa_addr);
            if (inet_ntop(AF_INET, &address->sin_addr, text, sizeof(text)) == nullptr)
                continue;
            const bool wireless = std::string_view{entry->ifa_name}.starts_with("wlan");
            if (found.empty() || (wireless && !foundWireless)) {
                found = text;
                foundWireless = wireless;
            }
        }
        freeifaddrs(interfaces);
        return found;
    }

} // namespace NextUiNetwork
