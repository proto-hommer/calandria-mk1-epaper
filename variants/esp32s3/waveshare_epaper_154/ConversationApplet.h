#pragma once

#include "configuration.h"

#ifdef MESHTASTIC_INCLUDE_INKHUD

/*

MENSAJES: the card itself is the list of conversations; a conversation is the thing inside it.

  LIST   : Grupo (channel 0), Público (channel 1) and any other channel in use first, then one row per
           direct conversation,
           newest first. Each row: name, how long ago, the start of the last message, and a dot when
           it has unread messages. Click opens the conversation (and marks it read).
  THREAD : the messages, oldest at the top, the newest one selected. Each: sender, time, hops (ours:
           delivery). Turning moves the selection, click answers it.
  REPLY  : the quoted message on top, the node's canned messages, and "Escribir con el teclado"
           (MenuApplet::composeTo, free text only). A canned message is sent right away.
BOOT goes back one level (REPLY -> THREAD -> LIST); from LIST, InkHudNavigator goes back to Inicio.

Messages come from the global MessageStore. Gaps of the stock InkHUD are filled here, as a MeshModule:
  - channel messages (incoming and ours) are stored by ThreadedMessageApplet, but only while that
    applet is active (and there is one for channels 0 and 1 only). We store them whenever no active
    ThreadedMessageApplet does it for that channel: each message ends up in the store exactly once;
  - our own DMs (sent from the node or the phone) are stored: the stock code only stores incoming ones;
  - the routing ACKs of what we sent mark it delivered / relayed / failed.
Reactions (a reply carrying an emoji) are stored as plain messages: the emoji, or "(emoji)" when the
font lacks it.

Unread counts live here (per channel and per peer) and are shown by Inicio: they only go down when the
conversation is opened, not when a message pops up on its own.

*/

#include "CalandriaUI.h"

#include "AppletNames.h"
#include "MenuAppletInclude.h"
#include "graphics/niche/Utils/CannedMessageStore.h"

#include "MeshModule.h"
#include "MeshService.h"
#include "MessageStore.h"
#include "NodeDB.h"
#include "Observer.h"
#include "RTC.h"
#include "Router.h"
#include "mesh/Channels.h"
#include "mesh/generated/meshtastic/mesh.pb.h"
#include "mesh/mesh-pb-constants.h"
#include "modules/TextMessageModule.h"

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <map>
#include <string>
#include <vector>

namespace NicheGraphics::InkHUD
{

class ConversationApplet : public CalandriaUI<Applet>, public MeshModule
{
  public:
    ConversationApplet() : MeshModule("ConversationApplet")
    {
        instance = this;
        loopbackOk = true; // our own sends and the ACKs our router raises arrive as local packets
    }

    static ConversationApplet *get() { return instance; }

    static uint16_t totalUnread()
    {
        uint16_t n = 0;
        for (uint16_t c : chanUnread)
            n += c;
        for (const auto &kv : dmUnread)
            n += kv.second;
        return n;
    }

    // Entered from Inicio: start at the list, on the first row
    void enter()
    {
        cursor = 0;
        setLevel(LIST);
    }

    void onActivate() override
    {
        textMessageObserver.observe(textMessageModule);
        setLevel(level);
    }
    void onDeactivate() override { textMessageObserver.unobserve(textMessageModule); }

    void onRender(bool full) override
    {
        (void)full;
        buildConversations();
        if (level == LIST)
            renderList();
        else if (level == THREAD)
            renderThread();
        else
            renderReply();
    }

    void onButtonShortPress() override
    {
        if (level == LIST) {
            if (cursor >= convs.size())
                return;
            open = convs[cursor];
            markRead(open);
            buildThread();
            msgCursor = thread.empty() ? 0 : thread.size() - 1;
            threadTop = 0;
            setLevel(THREAD);
            requestUpdate(EInk::UpdateTypes::FULL);
        } else if (level == THREAD) {
            buildThread(); // an empty conversation can still be written to (no quote then)
            replyCursor = 0;
            notice.clear();
            setLevel(REPLY);
            requestUpdate(EInk::UpdateTypes::FULL);
        } else {
            chooseReply();
        }
    }

    // THREAD / REPLY only (subscribed there)
    void onButtonLongPress() override { onExitShort(); }
    void onExitShort() override
    {
        if (level == REPLY)
            setLevel(THREAD);
        else
            setLevel(LIST);
        requestUpdate(EInk::UpdateTypes::FULL);
    }

    void onNavUp() override { move(-1); }
    void onNavDown() override { move(1); }

  protected:
    // ---------- MeshModule: our own DMs, and delivery of what we sent ----------
    bool wantPacket(const meshtastic_MeshPacket *p) override
    {
        if (p->which_payload_variant != meshtastic_MeshPacket_decoded_tag)
            return false;
        if (p->decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP)
            return isFromUs(p) || p->to == NODENUM_BROADCAST;
        return p->decoded.portnum == meshtastic_PortNum_ROUTING_APP && p->decoded.request_id != 0 && isToUs(p);
    }

    ProcessMessage handleReceived(const meshtastic_MeshPacket &mp) override
    {
        if (mp.decoded.portnum == meshtastic_PortNum_TEXT_MESSAGE_APP) {
            if (mp.to == NODENUM_BROADCAST)
                storeChannelMessage(mp);
            if (isFromUs(&mp))
                noteSent(mp);
        } else
            noteRouting(mp);
        return ProcessMessage::CONTINUE;
    }

  private:
    enum Level : uint8_t { LIST, THREAD, REPLY };
    struct Conv {
        bool dm = false;
        uint32_t id = 0; // channel index, or the peer's node number
        bool operator==(const Conv &o) const { return dm == o.dm && id == o.id; }
    };
    struct Sent { // one of our messages waiting for its ACK
        PacketId id;
        NodeNum dest;
        uint8_t channel;
        std::string text;
    };

    static inline ConversationApplet *instance = nullptr;
    static inline uint16_t chanUnread[8] = {0};
    static inline std::map<NodeNum, uint16_t> dmUnread;

    Level level = LIST;
    std::vector<Conv> convs;
    std::vector<const StoredMessage *> thread;
    Conv open;
    uint16_t cursor = 0, listTop = 0;  // LIST
    uint16_t msgCursor = 0, threadTop = 0; // THREAD
    uint16_t replyCursor = 0, replyTop = 0; // REPLY
    std::string notice;
    std::vector<Sent> sent;

    void setLevel(Level l)
    {
        level = l;
        setInputsSubscribed(BUTTON_SHORT | NAV_UP | NAV_DOWN, true);
        setInputsSubscribed(BUTTON_LONG | EXIT_SHORT, l != LIST);
    }

    // ---------- Unread ----------
    int onReceiveTextMessage(const meshtastic_MeshPacket *p)
    {
        if (!p || p->from == 0 || p->from == nodeDB->getNodeNum())
            return 0;
        Conv c;
        c.dm = p->to != NODENUM_BROADCAST;
        c.id = c.dm ? p->from : p->channel;
        const bool reading = isForeground() && level != LIST && open == c;
        if (!reading) {
            if (c.dm)
                dmUnread[c.id]++;
            else if (c.id < 8)
                chanUnread[c.id]++;
        }
        if (isForeground())
            requestUpdate(EInk::UpdateTypes::FAST);
        return 0;
    }
    CallbackObserver<ConversationApplet, const meshtastic_MeshPacket *> textMessageObserver =
        CallbackObserver<ConversationApplet, const meshtastic_MeshPacket *>(this, &ConversationApplet::onReceiveTextMessage);

    static uint16_t unreadOf(const Conv &c)
    {
        if (c.dm) {
            auto it = dmUnread.find(c.id);
            return it == dmUnread.end() ? 0 : it->second;
        }
        return c.id < 8 ? chanUnread[c.id] : 0;
    }

    static void markRead(const Conv &c)
    {
        if (c.dm)
            dmUnread.erase(c.id);
        else if (c.id < 8)
            chanUnread[c.id] = 0;
    }

    // ---------- Our own messages ----------
    void noteSent(const meshtastic_MeshPacket &mp)
    {
        const bool dm = mp.to != NODENUM_BROADCAST && mp.to != 0;
        // Broadcasts are stored by ThreadedMessageApplet; our DMs from the node are not stored by anyone
        if (dm && messageStore.shouldStorePacket(mp))
            if (const StoredMessage *m = messageStore.tryAddFromPacket(mp))
                const_cast<StoredMessage &>(*m).ackStatus = AckStatus::NONE; // until its ACK comes back
        Sent s;
        s.id = mp.id;
        s.dest = mp.to;
        s.channel = mp.channel;
        s.text.assign((const char *)mp.decoded.payload.bytes, strnlen((const char *)mp.decoded.payload.bytes,
                                                                      std::min<size_t>(mp.decoded.payload.size, 64)));
        if (sent.size() >= 8)
            sent.erase(sent.begin());
        sent.push_back(s);
        if (isForeground())
            requestUpdate(EInk::UpdateTypes::FAST);
    }

    // A channel message (anyone's): into the store, unless the channel's ThreadedMessageApplet does it
    void storeChannelMessage(const meshtastic_MeshPacket &mp)
    {
        if (channelAppletStores(mp.channel) || !messageStore.shouldStorePacket(mp))
            return;
        const StoredMessage *m = messageStore.tryAddFromPacket(mp);
        if (m && isFromUs(&mp))
            const_cast<StoredMessage &>(*m).ackStatus = AckStatus::NONE; // until someone repeats it
        if (isForeground())
            requestUpdate(EInk::UpdateTypes::FAST);
    }

    // Same test ThreadedMessageApplet::handleReceived() makes before storing: its name and isActive()
    bool channelAppletStores(uint8_t channel)
    {
        for (Applet *a : inkhud->userApplets)
            if (a && a->name && !strncmp(a->name, AppletNames::CHANNEL_PREFIX, AppletNames::CHANNEL_PREFIX_LEN) &&
                atoi(a->name + AppletNames::CHANNEL_PREFIX_LEN) == channel)
                return a->isActive();
        return false;
    }

    // ACK / NAK for one of ours: find the stored message (newest own one with that text) and mark it
    void noteRouting(const meshtastic_MeshPacket &mp)
    {
        auto it = std::find_if(sent.begin(), sent.end(), [&](const Sent &s) { return s.id == mp.decoded.request_id; });
        if (it == sent.end())
            return;
        meshtastic_Routing r = meshtastic_Routing_init_zero;
        if (!pb_decode_from_bytes(mp.decoded.payload.bytes, mp.decoded.payload.size, &meshtastic_Routing_msg, &r))
            return;
        AckStatus st;
        if (r.which_variant == meshtastic_Routing_error_reason_tag && r.error_reason != meshtastic_Routing_Error_NONE)
            st = AckStatus::NACKED;
        else if (getFrom(&mp) == it->dest)
            st = AckStatus::ACKED; // the destination itself
        else
            st = AckStatus::RELAYED; // someone repeated it (broadcasts: that is all we get)
        const NodeNum me = nodeDB->getNodeNum();
        const auto &all = messageStore.getLiveMessages();
        for (auto m = all.rbegin(); m != all.rend(); ++m) {
            if (m->sender != me || m->dest != it->dest || strncmp(MessageStore::getText(*m), it->text.c_str(), it->text.size()))
                continue;
            if (m->ackStatus != AckStatus::ACKED) // a later relay echo must not downgrade "delivered"
                const_cast<StoredMessage &>(*m).ackStatus = st; // the store's own element: no API to set it
            break;
        }
        if (st != AckStatus::RELAYED || it->dest == NODENUM_BROADCAST)
            sent.erase(it);
        if (isForeground())
            requestUpdate(EInk::UpdateTypes::FAST);
    }

    // ---------- Data ----------
    static bool channelUsed(uint8_t i) { return channels.getByIndex(i).role != meshtastic_Channel_Role_DISABLED; }

    static bool belongs(const StoredMessage &m, const Conv &c, NodeNum me)
    {
        if (!c.dm)
            return m.type == MessageType::BROADCAST && m.channelIndex == c.id;
        if (m.type == MessageType::BROADCAST)
            return false;
        return (m.sender == me ? m.dest : m.sender) == c.id;
    }

    void buildConversations()
    {
        const Conv keep = cursor < convs.size() ? convs[cursor] : Conv();
        convs.clear();
        for (uint8_t ch = 0; ch < 8; ch++) // every channel in use: their unread messages must be reachable
            if (channelUsed(ch)) {
                Conv c;
                c.id = ch;
                convs.push_back(c);
            }
        // Direct conversations, newest first (the store is in arrival order)
        const NodeNum me = nodeDB->getNodeNum();
        const auto &all = messageStore.getLiveMessages();
        for (auto m = all.rbegin(); m != all.rend(); ++m) {
            if (m->type == MessageType::BROADCAST)
                continue;
            Conv c;
            c.dm = true;
            c.id = m->sender == me ? m->dest : m->sender;
            if (std::find(convs.begin(), convs.end(), c) == convs.end())
                convs.push_back(c);
        }
        for (const auto &kv : dmUnread) { // unread but already pruned from the store
            Conv c;
            c.dm = true;
            c.id = kv.first;
            if (std::find(convs.begin(), convs.end(), c) == convs.end())
                convs.push_back(c);
        }
        for (uint16_t i = 0; i < convs.size(); i++)
            if (convs[i] == keep)
                cursor = i; // a new conversation on top must not move the selection
        if (cursor >= convs.size())
            cursor = convs.empty() ? 0 : convs.size() - 1;
    }

    void buildThread()
    {
        thread.clear();
        const NodeNum me = nodeDB->getNodeNum();
        for (const StoredMessage &m : messageStore.getLiveMessages())
            if (belongs(m, open, me))
                thread.push_back(&m);
        if (msgCursor >= thread.size())
            msgCursor = thread.empty() ? 0 : thread.size() - 1;
    }

    const StoredMessage *lastOf(const Conv &c)
    {
        const NodeNum me = nodeDB->getNodeNum();
        const auto &all = messageStore.getLiveMessages();
        for (auto m = all.rbegin(); m != all.rend(); ++m)
            if (belongs(*m, c, me))
                return &*m;
        return nullptr;
    }

    std::string convName(const Conv &c)
    {
        if (c.dm)
            return longNameOf(c.id);
        if (c.id < 2)
            return parse(c.id == 0 ? "Grupo" : "Público");
        return parse(fmt("Canal %u: ", (unsigned)c.id) + channels.getName(c.id));
    }

    static uint32_t ageOf(const StoredMessage &m)
    {
        if (m.isBootRelative)
            return millis() / 1000 >= m.timestamp ? millis() / 1000 - m.timestamp : 0;
        return secondsSince(m.timestamp);
    }

    // "14:05" today-ish, otherwise "hace 2 d"
    std::string timeOf(const StoredMessage &m)
    {
        const uint32_t s = ageOf(m);
        if (s == UINT32_MAX)
            return "";
        if (!m.isBootRelative && s < 18 * 3600UL) {
            time_t t = m.timestamp;
            struct tm lt;
            localtime_r(&t, &lt);
            return fmt("%02d:%02d", lt.tm_hour, lt.tm_min);
        }
        return "hace " + ago(s);
    }

    std::string hopsOf(NodeNum n)
    {
        const meshtastic_NodeInfoLite *node = nodeDB->getMeshNode(n);
        if (!node || !node->has_hops_away)
            return "";
        if (node->hops_away == 0)
            return "directo";
        return fmt("%u salto%s", (unsigned)node->hops_away, node->hops_away == 1 ? "" : "s");
    }

    // Delivery of one of ours; check = draw a tick before it
    std::string deliveryOf(const StoredMessage &m, bool *check)
    {
        *check = false;
        const bool dm = m.type != MessageType::BROADCAST;
        switch (m.ackStatus) {
        case AckStatus::ACKED:
            *check = true;
            return dm ? "entregado" : "enviado";
        case AckStatus::RELAYED:
            *check = true;
            return dm ? "retransmitido" : "enviado";
        case AckStatus::NACKED:
        case AckStatus::TIMEOUT:
            return "no entregado";
        default:
            return "enviando";
        }
    }

    // ---------- Navigation ----------
    void move(int dir)
    {
        uint16_t *pos;
        size_t n;
        if (level == LIST) {
            pos = &cursor;
            n = convs.size();
        } else if (level == THREAD) {
            pos = &msgCursor;
            n = thread.size();
        } else {
            pos = &replyCursor;
            n = replyCount();
        }
        const int next = (int)*pos + dir;
        if (next < 0 || next >= (int)n)
            return;
        *pos = (uint16_t)next;
        notice.clear();
        requestUpdate(EInk::UpdateTypes::FAST);
    }

    // ---------- Reply ----------
    uint8_t cannedCount()
    {
        CannedMessageStore *cm = CannedMessageStore::getInstance();
        return cm ? cm->size() : 0;
    }
    size_t replyCount() { return cannedCount() + 1; } // + "Escribir con el teclado"

    void replyTarget(NodeNum *dest, uint8_t *channel)
    {
        if (open.dm) {
            *dest = open.id;
            *channel = nodeDB->getMeshNodeChannel(open.id);
            if (!thread.empty())
                *channel = thread.back()->channelIndex;
        } else {
            *dest = NODENUM_BROADCAST;
            *channel = (uint8_t)open.id;
        }
    }

    void chooseReply()
    {
        buildThread(); // fresh pointers: the store may have dropped old messages since the last render
        NodeNum dest;
        uint8_t channel;
        replyTarget(&dest, &channel);
        if (replyCursor < cannedCount()) {
            sendText(dest, channel, CannedMessageStore::getInstance()->at(replyCursor).c_str());
            setLevel(THREAD);
            msgCursor = UINT16_MAX; // jump to the newest (ours) once it is stored
            requestUpdate(EInk::UpdateTypes::FULL);
            return;
        }
        // Free text: the existing on-screen keyboard (MenuApplet), straight to this recipient
        setLevel(THREAD);
        msgCursor = UINT16_MAX;
        inkhud->openMenu();
        MenuApplet *menu = (MenuApplet *)inkhud->getSystemApplet("Menu");
        if (menu)
            menu->composeTo(dest, channel, true);
    }

    // Same packet as MenuApplet::sendText (which is private there)
    static void sendText(NodeNum dest, uint8_t channel, const char *message)
    {
        meshtastic_MeshPacket *p = router->allocForSending();
        if (!p)
            return;
        p->decoded.portnum = meshtastic_PortNum_TEXT_MESSAGE_APP;
        p->to = dest;
        p->channel = channel;
        p->want_ack = true;
        p->decoded.payload.size = std::min<size_t>(strlen(message), sizeof(p->decoded.payload.bytes));
        memcpy(p->decoded.payload.bytes, message, p->decoded.payload.size);
        LOG_INFO("Send message id=%d, dest=%x, msg=%.*s", p->id, p->to, p->decoded.payload.size, p->decoded.payload.bytes);
        service->sendToMesh(p, RX_SRC_LOCAL, true); // to the mesh, copy to the phone
    }

    // ---------- Drawing ----------
    void renderList()
    {
        const uint16_t unread = totalUnread();
        drawBar("MENSAJES", unread ? std::to_string(unread) + " sin leer" : "", true);
        const int16_t top = barHeight() + 1;
        if (convs.empty()) {
            setFont(fontSmall);
            printAt(X(0.5f), Y(0.5f), "Sin conversaciones", CENTER, MIDDLE);
            return;
        }
        const uint16_t rowH = fontMedium.lineHeight() + fontSmall.lineHeight() + 6;
        const uint16_t rows = std::max<uint16_t>(1, (height() - top) / rowH);
        if (cursor < listTop)
            listTop = cursor;
        if (cursor >= listTop + rows)
            listTop = cursor - rows + 1;

        for (uint16_t r = 0; r < rows && listTop + r < convs.size(); r++) {
            const uint16_t i = listTop + r;
            const Conv &c = convs[i];
            const int16_t y = top + r * rowH;
            if (r > 0)
                dottedLine(y);
            const uint16_t fg = beginRow(y, rowH, i == cursor);
            const StoredMessage *last = lastOf(c);
            const bool unreadHere = unreadOf(c) > 0;

            int16_t nameX = 4;
            if (unreadHere) { // the dot
                fillCircle(7, y + 3 + fontMedium.lineHeight() / 2, 3, fg);
                nameX = 14;
            }
            setFont(fontSmall);
            const std::string when = last ? timeOf(*last) : "";
            const uint16_t whenW = when.empty() ? 0 : getTextWidth(when) + 6;
            if (!when.empty())
                printAt(width() - 3, y + 3, when, RIGHT, TOP);
            setFont(fontMedium);
            setCrop(nameX, y, width() - nameX - whenW, rowH);
            printAt(nameX, y + 2, convName(c));
            resetCrop();

            std::string preview;
            if (!last)
                preview = "Sin mensajes";
            else {
                const NodeNum me = nodeDB->getNodeNum();
                std::string who = last->sender == me ? "Vos: " : c.dm ? "" : shortNameOf(last->sender) + ": ";
                preview = who + readable(MessageStore::getText(*last));
            }
            setFont(fontSmall);
            setCrop(4, y, width() - 8, rowH);
            printAt(4, y + 3 + fontMedium.lineHeight(), preview);
            resetCrop();
            setTextColor(BLACK);
        }
        drawScrollHints(listTop > 0, listTop + rows < convs.size());
    }

    uint16_t blockHeight(const StoredMessage &m, uint16_t textW)
    {
        setFont(fontSmall);
        return fontSmall.lineHeight() + getWrappedTextHeight(4, textW, readable(MessageStore::getText(m))) + 6;
    }

    void renderThread()
    {
        buildThread();
        if (msgCursor == UINT16_MAX || msgCursor >= thread.size())
            msgCursor = thread.empty() ? 0 : thread.size() - 1;
        if (isForeground())
            markRead(open); // reading it now
        drawBar(convName(open), thread.empty() ? "" : fmt("%u/%u", msgCursor + 1, (unsigned)thread.size()), true);
        const int16_t top = barHeight() + 2;
        const uint16_t avail = height() - top;
        const uint16_t textW = width() - 8;
        if (thread.empty()) {
            setFont(fontSmall);
            printAt(X(0.5f), Y(0.5f), "Sin mensajes. Clic: escribir", CENTER, MIDDLE);
            return;
        }

        // First message shown: keep the selected one fully on screen
        if (msgCursor < threadTop)
            threadTop = msgCursor;
        for (;;) {
            uint32_t h = 0;
            for (uint16_t i = threadTop; i <= msgCursor; i++)
                h += blockHeight(*thread[i], textW);
            if (h <= avail || threadTop == msgCursor)
                break;
            threadTop++;
        }
        // Room left below the selection: pull older messages in from above
        for (;;) {
            if (threadTop == 0)
                break;
            uint32_t h = 0;
            for (uint16_t i = threadTop - 1; i < thread.size(); i++)
                h += blockHeight(*thread[i], textW);
            if (h > avail)
                break;
            threadTop--;
        }

        const NodeNum me = nodeDB->getNodeNum();
        int16_t y = top;
        setCrop(0, top, width(), avail);
        for (uint16_t i = threadTop; i < thread.size() && y < height(); i++) {
            const StoredMessage &m = *thread[i];
            const uint16_t h = blockHeight(m, textW);
            const uint16_t fg = beginRow(y, h, i == msgCursor);
            const bool ours = m.sender == me;
            std::string head = ours ? "Vos" : shortNameOf(m.sender);
            const std::string t = timeOf(m);
            if (!t.empty())
                head += " · " + t;
            setFont(fontSmall);
            int16_t x = 4;
            if (ours) {
                bool check;
                const std::string d = deliveryOf(m, &check);
                head += " · ";
                printAt(x, y + 2, parse(head));
                x += getTextWidth(parse(head));
                if (check) {
                    drawCheck(x, y + 2 + fontSmall.lineHeight() / 2, fg);
                    x += 12;
                }
                printAt(x, y + 2, d);
            } else {
                const std::string hops = hopsOf(m.sender);
                if (!hops.empty())
                    head += " · " + hops;
                printAt(x, y + 2, parse(head));
            }
            // underline the header, dotted
            for (int16_t dx = 4; dx < width() - 4; dx += 2)
                drawPixel(dx, y + 2 + fontSmall.lineHeight(), fg);
            printWrapped(4, y + 4 + fontSmall.lineHeight(), textW, readable(MessageStore::getText(m)));
            setTextColor(BLACK);
            y += h;
        }
        resetCrop();
        drawScrollHints(threadTop > 0, y > height());
    }

    void renderReply()
    {
        buildThread();
        drawBar("Responder", convName(open), true);
        int16_t y = barHeight() + 3;
        setFont(fontSmall);

        // The quote: the selected message, framed, at most three lines
        if (!thread.empty() && msgCursor < thread.size()) {
            const StoredMessage &q = *thread[msgCursor];
            const NodeNum me = nodeDB->getNodeNum();
            const std::string text =
                (q.sender == me ? std::string("Vos") : shortNameOf(q.sender)) + ": " + readable(MessageStore::getText(q));
            const uint16_t maxH = fontSmall.lineHeight() * 3 + 2;
            const uint16_t h = std::min<uint16_t>(maxH, getWrappedTextHeight(6, width() - 12, text)) + 4;
            for (int16_t k = 0; k < 2; k++)
                drawLine(2 + k, y, 2 + k, y + h, BLACK); // quote mark: a bar on the left
            setCrop(6, y, width() - 8, h);
            printWrapped(8, y + 2, width() - 12, text);
            resetCrop();
            y += h + 4;
        }

        const uint16_t rowH = fontSmall.lineHeight() + 6;
        const uint16_t rows = std::max<uint16_t>(1, (height() - y) / rowH);
        const size_t n = replyCount();
        if (replyCursor < replyTop)
            replyTop = replyCursor;
        if (replyCursor >= replyTop + rows)
            replyTop = replyCursor - rows + 1;
        for (uint16_t r = 0; r < rows && replyTop + r < n; r++) {
            const uint16_t i = replyTop + r;
            const int16_t ry = y + r * rowH;
            const uint16_t fg = beginRow(ry, rowH, i == replyCursor);
            const bool keyboard = i == cannedCount();
            const std::string label = keyboard ? parse("Escribir con el teclado") : parse(CannedMessageStore::getInstance()->at(i));
            setCrop(0, ry, width() - 14, rowH);
            printAt(6, ry + rowH / 2, label, LEFT, MIDDLE);
            resetCrop();
            if (keyboard)
                drawChevron(width() - 9, ry + rowH / 2, fg);
            setTextColor(BLACK);
        }
        drawScrollHints(replyTop > 0, replyTop + rows < n);
    }
};

} // namespace NicheGraphics::InkHUD

#endif
