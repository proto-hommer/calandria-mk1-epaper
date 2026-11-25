#pragma once

#include "configuration.h"

/*

Identity beacon for CalandriApp: lets the app recognise Calandria MK1 epaper nodes on the mesh.

The official HardwareModel enum has no entry for this board (we report PRIVATE_HW), so we announce it on
a private port. The official apps ignore unknown ports; any app connected to a node that hears this
packet receives it through the normal FromRadio stream.

Port:    CALANDRIA_PORT (323 = PRIVATE_APP + 67), primary channel, broadcast, no ack.
Payload: UTF-8 text, fields separated by ';'
         CALANDRIA/1;model=Calandria MK1 epaper;fw=<firmware version>;env=<pio env>;ver=<CALANDRIA_VERSION>
         (contract with CalandriApp: new fields go at the end, never reorder)
When:    5 min after boot, then every 6 h, skipped while the channel is busy. A request on this port
         with want_response is answered directly: always when it comes from our own phone (to the local
         node), otherwise at most once a minute and never to relayed broadcasts.

Update notice: on connecting, CalandriApp (which can see GitHub releases) sends the LOCAL node, from our
own phone and without want_response:
         CALANDRIA/1;latest=<release tag>;commit=<full sha>
Kept in RAM (the app resends it on every connection). If the commit does not start with our own build
hash (last part of APP_VERSION), Estado shows "Firmware nuevo: <version>". Ignored from the mesh.

*/

#include "MeshService.h"
#include "SinglePortModule.h"
#include "airtime.h"
#include "concurrency/OSThread.h"
#include "mesh/Throttle.h"

#include <cctype>
#include <cstdio>
#include <cstring>

#ifdef MESHTASTIC_INCLUDE_INKHUD
#include "graphics/niche/InkHUD/Applets/System/Notification/NotificationApplet.h"
#include "graphics/niche/InkHUD/InkHUD.h"
#endif

constexpr meshtastic_PortNum CALANDRIA_PORT = (meshtastic_PortNum)(meshtastic_PortNum_PRIVATE_APP + 67);
constexpr const char *CALANDRIA_MODEL = "Calandria MK1 epaper";
// The Calandria's own semantic version (alpha: 0.x). APP_VERSION stays Meshtastic's (2.8.3.<hash>): apps use it
// to know which features the firmware has. ws154.1 .. ws154.5 = 0.1.0 .. 0.5.0
constexpr const char *CALANDRIA_VERSION = "0.6.0";

// Latest release, as told by CalandriApp. Empty until the phone sends it.
struct CalandriaLatest {
    static inline char tag[33] = "";    // "calandria-0.6.1"
    static inline char commit[41] = ""; // hex sha

    // Our build hash: "2.8.3.edc1ef7" -> "edc1ef7"
    static const char *ownHash()
    {
        const char *v = optstr(APP_VERSION);
        const char *dot = strrchr(v, '.');
        return dot ? dot + 1 : v;
    }

    static bool updateAvailable()
    {
        const char *own = ownHash();
        const size_t n = strlen(own);
        if (!tag[0] || !commit[0] || n < 7 || n > 40)
            return false;
        for (size_t i = 0; i < n; i++)
            if (tolower((unsigned char)own[i]) != tolower((unsigned char)commit[i]))
                return true; // commit[] is NUL-padded: a shorter commit also mismatches here
        return false;
    }

    // "calandria-0.6.1" -> "0.6.1"; old scheme "v2.8.3-ws154.5" -> "ws154.5"
    static const char *versionLabel()
    {
        static const char prefix[] = "calandria-";
        if (strncmp(tag, prefix, sizeof(prefix) - 1) == 0)
            return tag + sizeof(prefix) - 1;
        const char *dash = strrchr(tag, '-');
        return dash ? dash + 1 : tag;
    }
};

class CalandriaBeacon : public SinglePortModule, private concurrency::OSThread
{
  public:
    CalandriaBeacon() : SinglePortModule("CalandriaBeacon", CALANDRIA_PORT), concurrency::OSThread("CalandriaBeacon")
    {
        setIntervalFromNow(FIRST_BEACON_MS);
    }

  protected:
    // Only the "latest release" note from our own phone (from == 0). Anything else on this port, e.g.
    // other nodes' beacons, can come from anyone on the mesh: ignored.
    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        if (mp.from == 0 && mp.which_payload_variant == meshtastic_MeshPacket_decoded_tag)
            readLatest((const char *)mp.decoded.payload.bytes, mp.decoded.payload.size);
        return ProcessMessage::CONTINUE;
    }

    int32_t runOnce() override
    {
        if (airTime && !airTime->isTxAllowedChannelUtil(true)) {
            LOG_DEBUG("CalandriaBeacon: channel busy, retry later");
            return RETRY_MS;
        }
        meshtastic_MeshPacket *p = allocIdentity();
        if (p) {
            p->to = NODENUM_BROADCAST;
            p->channel = 0; // primary
            p->want_ack = false;
            p->priority = meshtastic_MeshPacket_Priority_BACKGROUND;
            LOG_INFO("CalandriaBeacon: announce %.*s", p->decoded.payload.size, p->decoded.payload.bytes);
            service->sendToMesh(p, RX_SRC_LOCAL, false);
        }
        return PERIOD_MS;
    }

    // Direct answer to an app asking "what are you?" on our port.
    // From our own phone (request to the LOCAL node, from == 0): always answer, it never goes on air.
    // The app asks on every connection, and a throttled request would get a NO_RESPONSE NAK instead.
    // From the mesh: never to relayed broadcasts, at most once a minute.
    meshtastic_MeshPacket *allocReply() override
    {
        if (currentRequest && currentRequest->from == 0)
            return allocIdentity();
        if (isMultiHopBroadcastRequest())
            return nullptr;
        if (lastReplyMs && Throttle::isWithinTimespanMs(lastReplyMs, REPLY_MIN_INTERVAL_MS))
            return nullptr;
        lastReplyMs = millis();
        return allocIdentity();
    }

  private:
    static constexpr uint32_t FIRST_BEACON_MS = 5 * 60 * 1000UL;
    static constexpr uint32_t PERIOD_MS = 6 * 60 * 60 * 1000UL;
    static constexpr uint32_t RETRY_MS = 10 * 60 * 1000UL;
    static constexpr uint32_t REPLY_MIN_INTERVAL_MS = 60 * 1000UL;

    uint32_t lastReplyMs = 0;
    bool updateNotified = false; // pop-up once per boot

    // "CALANDRIA/1;latest=<tag>;commit=<sha>": both fields required, checked before anything is kept
    void readLatest(const char *data, size_t len)
    {
        static const char head[] = "CALANDRIA/1;";
        if (len < sizeof(head) - 1 || len >= 200 || strncmp(data, head, sizeof(head) - 1) != 0)
            return;
        char tag[sizeof(CalandriaLatest::tag)] = "", commit[sizeof(CalandriaLatest::commit)] = "";
        bool okTag = false, okCommit = false;
        size_t i = sizeof(head) - 1;
        while (i < len) {
            size_t end = i;
            while (end < len && data[end] != ';')
                end++;
            const char *f = data + i;
            const size_t flen = end - i;
            if (flen > 7 && strncmp(f, "latest=", 7) == 0)
                okTag = copyField(f + 7, flen - 7, tag, sizeof(tag), false);
            else if (flen > 7 && strncmp(f, "commit=", 7) == 0)
                okCommit = copyField(f + 7, flen - 7, commit, sizeof(commit), true) && strlen(commit) >= 7;
            i = end + 1;
        }
        if (!okTag || !okCommit)
            return;
        strcpy(CalandriaLatest::tag, tag);
        strcpy(CalandriaLatest::commit, commit);
        const bool update = CalandriaLatest::updateAvailable();
        LOG_INFO("CalandriaBeacon: latest %s (%s), ours %s: %s", tag, commit, CalandriaLatest::ownHash(),
                 update ? "update available" : "up to date");
#ifdef MESHTASTIC_INCLUDE_INKHUD
        if (update && !updateNotified) {
            updateNotified = true;
            auto *note = (NicheGraphics::InkHUD::NotificationApplet *)NicheGraphics::InkHUD::InkHUD::getInstance()
                             ->getSystemApplet("Notification");
            if (note) {
                char text[48];
                snprintf(text, sizeof(text), "Firmware nuevo: %s", CalandriaLatest::versionLabel());
                note->showText(text);
            }
        }
#endif
    }

    // Tag: letters, digits, '.', '-', '_'. Commit: hex only. False if empty, too long or anything else.
    static bool copyField(const char *src, size_t n, char *dst, size_t size, bool hex)
    {
        if (n == 0 || n >= size)
            return false;
        for (size_t k = 0; k < n; k++) {
            const unsigned char c = (unsigned char)src[k];
            const bool ok = hex ? isxdigit(c) : (isalnum(c) || c == '.' || c == '-' || c == '_');
            if (!ok)
                return false;
            dst[k] = (char)tolower(c);
        }
        dst[n] = '\0';
        return true;
    }

    meshtastic_MeshPacket *allocIdentity()
    {
        meshtastic_MeshPacket *p = allocDataPacket();
        if (!p)
            return nullptr;
        int n = snprintf((char *)p->decoded.payload.bytes, sizeof(p->decoded.payload.bytes),
                         "CALANDRIA/1;model=%s;fw=%s;env=%s;ver=%s", CALANDRIA_MODEL, optstr(APP_VERSION), optstr(APP_ENV),
                         CALANDRIA_VERSION);
        p->decoded.payload.size = (n > 0 && (size_t)n < sizeof(p->decoded.payload.bytes)) ? n : 0;
        return p;
    }
};
