// SpeedBreaker runtime. GPL-3.0-or-later (see COPYING).
//
// The Online page of the settings menu: the lobby (net/lobby.h). Not connected: the server address and the player's name, Connect. Connected: the
// rooms on the server, to join (with a password if it has one), and Create Room. In a room: who is in it and who is ready; the host chooses the
// race (mode, track, laps, traffic) and starts it, the guest says Ready. Drawn under the Online Play option, only when that is on.
#include <stdafx.h>
#include "ui.h"

#include <net/lobby.h>
#include <user/settings.h>

#include <imgui.h>

namespace ui
{
    namespace
    {
        char s_server[96];
        char s_name[24];
        char s_roomName[20];
        char s_password[20];
        char s_joinPassword[20];
        bool s_inited = false;
        int s_selected = -1;     // the room list's selection
        int s_mode = 0;          // the race the host is setting up
        int s_track = 1;
        int s_laps = 2;
        int s_traffic = 0;
        bool s_setupLoaded = false;

        constexpr const char* kModes[] = { "Circuit", "Sprint", "Lap Knockout", "Drag" };
        constexpr const char* kTraffic[] = { "Minimum", "Medium", "Heavy" };

        void Init()
        {
            if (s_inited)
                return;
            s_inited = true;
            snprintf(s_server, sizeof(s_server), "%s", lobby::SavedServer().c_str());
            snprintf(s_name, sizeof(s_name), "%s", lobby::SavedName().c_str());
            snprintf(s_roomName, sizeof(s_roomName), "%s", "race1");
        }

        json::Value CurrentSetup()
        {
            json::Value v = json::Value::Object();
            v["mode"] = kModes[s_mode];
            v["track"] = s_track;
            v["laps"] = s_laps;
            v["traffic"] = kTraffic[s_traffic];
            return v;
        }

        std::string Describe(const json::Value& setup)
        {
            if (!setup.IsObject() || setup.o.empty())
                return "no race chosen yet";
            return setup.Str("mode", "?") + ", track " + std::to_string(int(setup.Num("track", 1))) + ", " +
                std::to_string(int(setup.Num("laps", 2))) + " laps, traffic " + setup.Str("traffic", "Minimum");
        }
    }

    void DrawOnlinePage()
    {
        Init();
        if (!settings::GetBool(settings::Id::OnlinePlay))
        {
            ImGui::Spacing();
            ImGui::TextWrapped("Turn Online Play on above and restart the game to use the lobby.");
            return;
        }
        const lobby::Snapshot state = lobby::Get();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        if (!state.error.empty())
        {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.0f, 0.55f, 0.45f, 1.0f));
            ImGui::TextWrapped("%s", state.error.c_str());
            ImGui::PopStyleColor();
            ImGui::Spacing();
        }

        // Not connected.
        if (!state.connected)
        {
            ImGui::TextUnformatted("Server");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##server", s_server, sizeof(s_server));
            ImGui::TextUnformatted("Your name");
            ImGui::SetNextItemWidth(-1);
            ImGui::InputText("##name", s_name, sizeof(s_name));
            ImGui::Spacing();
            ImGui::BeginDisabled(state.connecting);
            if (ImGui::Button(state.connecting ? "Connecting..." : "Connect"))
                lobby::Connect(s_server, s_name);
            ImGui::EndDisabled();
            return;
        }

        // In a room.
        if (state.inRoom)
        {
            const lobby::Room& room = state.room;
            const bool host = state.you == 0;
            ImGui::Text("Room %s%s", room.name.c_str(), room.locked ? " (password)" : "");
            ImGui::Spacing();
            for (const lobby::Member& m : room.members)
                ImGui::Text("  %s: %s", m.name.c_str(), m.slot == 0 ? "host" : (m.ready ? "ready" : "not ready"));
            if (room.members.size() < 2)
                ImGui::TextDisabled("  waiting for a second player...");
            ImGui::Spacing();
            ImGui::TextWrapped("Race: %s", Describe(room.setup).c_str());
            ImGui::Spacing();
            if (state.starting)
            {
                ImGui::Separator();
                ImGui::TextWrapped("The race is on. Close this menu and go to Quick Race, Split Screen, choose the race above, and at \"Select player 2\" press F8; "
                                   "the guest then presses START. (Starting it for you comes later.)");
                if (ImGui::Button("End match"))
                    lobby::EndMatch();
                return;
            }
            if (host)
            {
                if (!s_setupLoaded)
                {
                    s_setupLoaded = true;
                    s_track = std::max(1, int(room.setup.Num("track", 1)));
                    s_laps = std::clamp(int(room.setup.Num("laps", 2)), 1, 9);
                }
                bool changed = false;
                ImGui::SetNextItemWidth(200);
                changed |= ImGui::Combo("Mode", &s_mode, kModes, IM_ARRAYSIZE(kModes));
                ImGui::SetNextItemWidth(200);
                changed |= ImGui::SliderInt("Track", &s_track, 1, 28);
                ImGui::SetNextItemWidth(200);
                changed |= ImGui::SliderInt("Laps", &s_laps, 1, 9);
                ImGui::SetNextItemWidth(200);
                changed |= ImGui::Combo("Traffic", &s_traffic, kTraffic, IM_ARRAYSIZE(kTraffic));
                if (changed)
                    lobby::SetSetup(CurrentSetup());
                ImGui::Spacing();
                const bool guestReady = room.members.size() >= 2 && room.members[1].ready;
                ImGui::BeginDisabled(!guestReady);
                if (ImGui::Button("Start race"))
                    lobby::Start();
                ImGui::EndDisabled();
                if (!guestReady)
                {
                    ImGui::SameLine();
                    ImGui::TextDisabled(room.members.size() < 2 ? "needs a second player" : "needs the other player to be ready");
                }
            }
            else
            {
                const bool ready = state.you < int(room.members.size()) && room.members[state.you].ready;
                if (ImGui::Button(ready ? "Not ready" : "Ready"))
                    lobby::SetReady(!ready);
            }
            ImGui::SameLine();
            if (ImGui::Button("Leave room"))
            {
                s_setupLoaded = false;
                lobby::Leave();
            }
            return;
        }

        // Connected, no room: the list, and a new one.
        ImGui::Text("Rooms on %s", state.server.c_str());
        ImGui::Spacing();
        if (state.rooms.empty())
            ImGui::TextDisabled("  none yet: create one below");
        for (int i = 0; i < int(state.rooms.size()); i++)
        {
            const lobby::Room& r = state.rooms[i];
            char label[160];
            snprintf(label, sizeof(label), "%s  (%d/%d)%s%s  %s", r.name.c_str(), r.players, r.max, r.locked ? "  [password]" : "",
                r.state == "racing" ? "  [racing]" : "", Describe(r.setup).c_str());
            if (ImGui::Selectable(label, s_selected == i))
                s_selected = i;
        }
        if (s_selected >= int(state.rooms.size()))
            s_selected = -1;
        ImGui::Spacing();
        const bool pick = s_selected >= 0 && s_selected < int(state.rooms.size());
        if (pick && state.rooms[s_selected].locked)
        {
            ImGui::SetNextItemWidth(200);
            ImGui::InputText("Password", s_joinPassword, sizeof(s_joinPassword), ImGuiInputTextFlags_Password);
        }
        ImGui::BeginDisabled(!pick || state.rooms[s_selected].state != "lobby" || state.rooms[s_selected].players >= state.rooms[s_selected].max);
        if (ImGui::Button("Join"))
            lobby::Join(state.rooms[s_selected].name, s_joinPassword);
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Refresh"))
            lobby::Refresh();
        ImGui::Spacing();
        ImGui::Separator();
        ImGui::Spacing();
        ImGui::TextUnformatted("New room");
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Name", s_roomName, sizeof(s_roomName));
        ImGui::SetNextItemWidth(200);
        ImGui::InputText("Password (optional)", s_password, sizeof(s_password), ImGuiInputTextFlags_Password);
        ImGui::BeginDisabled(s_roomName[0] == 0);
        if (ImGui::Button("Create room"))
        {
            s_setupLoaded = false;
            lobby::Create(s_roomName, s_password, CurrentSetup());
        }
        ImGui::EndDisabled();
        ImGui::SameLine();
        if (ImGui::Button("Disconnect"))
            lobby::Disconnect();
    }
}
