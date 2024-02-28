#include <iostream>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <list>

#include <raylib.h>

#define SRCERY_BLACK Color{28, 27, 25, 255}
#define SRCERY_RED Color{239, 47, 39, 255}
#define SRCERY_GREEN Color{81, 159, 80, 255}
#define SRCERY_YELLOW Color{251, 184, 41, 255}
#define SRCERY_BLUE Color{44, 120, 191, 255}
#define SRCERY_MAGENTA Color{224, 44, 109, 255}
#define SRCERY_CYAN Color{10, 174, 179, 255}
#define SRCERY_WHITE Color{186, 166, 127, 255}
#define SRCERY_BRIGHTBLACK Color{145, 129, 117, 255}
#define SRCERY_BRIGHTRED Color{247, 83, 65, 255}
#define SRCERY_BRIGHTGREEN Color{152, 188, 55, 255}
#define SRCERY_BRIGHTYELLOW Color{254, 208, 110}
#define SRCERY_BRIGHTBLUE Color{104, 168, 228, 255}
#define SRCERY_BRIGHTMAGENTA Color{255, 92, 143, 255}
#define SRCERY_BRIGHTCYAN Color{43, 228, 208, 255}
#define SRCERY_BRIGHTWHITE Color{252, 232, 195, 255}

#define WINDOW_WIDTH 1000
#define WINDOW_HEIGHT 600

struct Log {
    int term;
    size_t index;
    std::string log;
};

enum class ServerState {
    Leader,
    Follower,
    Candidate,
};

enum class RPCType {
    RequestVote,
    RequestVoteResponse,
    AppendEntries,
    AppendEntriesResponse,
};

struct RequestVote {
    int term;
    int candidate_id;
    size_t last_log_index;
    int last_log_term;
};

struct RequestVoteResponse {
    int term;
    bool vote_granted;
};

struct AppendEntries {
    int term;
    int leader_id;
    size_t prev_log_index;
    int prev_log_term;
    Log* entries;
    size_t entry_count;
    size_t leader_commit_index;
};

struct AppendEntriesResponse {
    int term;
    bool success;
};

struct RPC {
    RPCType type;
    union {
        RequestVote request_vote;
        RequestVoteResponse request_vote_response;
        AppendEntries append_entries;
        AppendEntriesResponse append_entries_response;
    };
};

struct Server {
    int id;  // ids start at 0
    std::vector<Server*> others;
    ServerState state {ServerState::Follower};
    float election_timer {0.0f};
    
    // persistent storage
    int term {0};
    int voted_for; // 0 indicates no vote
    std::vector<Log> log;

    // volatile state
    size_t commit_index;
    size_t last_applied;

    // volatide leader-only state


    // simulation state -- not part of raft
    float heartbeat_timer;
    std::list<RPC> messages;
    int vote_count {0};
};

#define SERVER_COUNT 5

#define ELECTION_TIMEOUT 5.0f

AppendEntriesResponse server_send_append_entries(Server& server, AppendEntries append_entries) {
    // TODO: we should really process the message indirectly
    assert(server.state != ServerState::Leader);

    if (server.state == ServerState::Candidate) {
        server.state = ServerState::Follower;
    }
    server.election_timer = 1.0f + ELECTION_TIMEOUT * (float)rand() / (float)RAND_MAX;


    server.election_timer = 1.0f + ELECTION_TIMEOUT * (float)rand() / (float)RAND_MAX;
    if (append_entries.term < server.term) {
        return AppendEntriesResponse { server.term, false };
    }
    if (server.log.size() <= append_entries.prev_log_index - 1) {
        return AppendEntriesResponse { server.term, false };
    }
    Log log = server.log[append_entries.prev_log_index - 1];
    if (log.term != append_entries.prev_log_term) {
        return AppendEntriesResponse { server.term, false };
    }
    if (log.term != append_entries.term) {
        server.log.resize(append_entries.prev_log_index - 1); // not sure about - 1 here
    }
    for (size_t i = 0; i < append_entries.entry_count; ++i) {
        server.log.push_back(append_entries.entries[i]);
    }
    if (append_entries.leader_commit_index > server.commit_index) {
        if (append_entries.entry_count == 0) {
            server.commit_index = append_entries.leader_commit_index;
        } else {
            server.commit_index = std::min(append_entries.leader_commit_index, append_entries.entries[append_entries.entry_count - 1].index);
        }
    }
    return AppendEntriesResponse{ server.term, true };
}

RequestVoteResponse server_send_request_vote(Server& server, RequestVote request_vote) {
    RequestVoteResponse response {};
    if (server.term > request_vote.term) {
        response.term = server.term;
        response.vote_granted = false;
    } else if ((server.voted_for == 0 || server.voted_for == request_vote.candidate_id) && (server.log.size() == 0 || request_vote.last_log_term >= server.log.back().term) && (server.log.size() == 0 || request_vote.last_log_index >= server.log.back().index)) {
        response.term = server.term;
        server.voted_for = request_vote.candidate_id;
        response.vote_granted = true;
    } else {
        response.term = server.term;
        response.vote_granted = false;
    }
    return response;
}

void server_send_rpc(Server& server, RPC rpc) {
    server.messages.push_back(rpc);
}

void answer_rpc(Server& server, RPC rpc) {
    switch (rpc.type) {
        case RPCType::RequestVote: {
            RequestVoteResponse request_vote_response = server_send_request_vote(server, rpc.request_vote);
            auto s = std::find_if(begin(server.others), end(server.others), [rv = rpc.request_vote](Server* s) {
                return s->id == rv.candidate_id;
            });
            RPC response;
            response.type = RPCType::RequestVoteResponse;
            response.request_vote_response = request_vote_response;
            server_send_rpc(**s, response);
        } break;
        case RPCType::AppendEntries: {
            AppendEntriesResponse append_entries_response = server_send_append_entries(server, rpc.append_entries);
            RPC response;
            response.type = RPCType::AppendEntriesResponse;
            response.append_entries_response = append_entries_response;
            auto s = std::find_if(begin(server.others), end(server.others), [rv = rpc.request_vote](Server* s) {
                return s->id == rv.candidate_id;
            });
            server_send_rpc(**s, response);

        } break;
        case RPCType::RequestVoteResponse: {
            assert(server.state == ServerState::Candidate || server.state == ServerState::Leader);
            RequestVoteResponse request_vote_response = rpc.request_vote_response;
            if (request_vote_response.vote_granted) {
                assert(request_vote_response.term <= server.term);
                server.vote_count++;
            }
            if (server.vote_count * 2 > SERVER_COUNT) {
                // majority of votes, become the leader
                server.state = ServerState::Leader;
                server.heartbeat_timer = 0.0f;
            }
        } break;
        case RPCType::AppendEntriesResponse: {
            assert(server.state == ServerState::Leader);
        } break;
        default:
            assert(false);
    }
}

void update_leader(Server& server, float dt) {
    server.heartbeat_timer -= dt;

    if (server.heartbeat_timer <= 0.0f) {
        server.heartbeat_timer = 3.0f * (float)rand() / (float)RAND_MAX;
        for (Server* other: server.others) {
            AppendEntries append_entries = AppendEntries {
                server.term,
                server.id,
                server.log.size() == 0 ? 0 : server.log.back().index,
                server.log.size() == 0 ? 0 : server.log.back().term,
                nullptr,
                0,
                server.commit_index,
            };
            RPC rpc;
            rpc.type = RPCType::AppendEntries;
            rpc.append_entries = append_entries;
            server_send_rpc(*other, rpc);
        }
    }
}

void update_candidate(Server& server, float dt) {
    for (RPC rpc: server.messages) {
        answer_rpc(server, rpc);
    }
    server.messages.clear();
}

void update_follower(Server& server, float dt) {
    server.election_timer -= dt;

    for (RPC rpc: server.messages) {
        answer_rpc(server, rpc);
    }
    server.messages.clear();
    
    if (server.election_timer <= 0.0f) {
        // start the election
        server.term++;
        server.voted_for = server.id;
        server.state = ServerState::Candidate;
        for (Server* other: server.others) {
            server_send_rpc(*other, RPC {
                RPCType::RequestVote,
                RequestVote {server.term, server.id, server.log.size() == 0 ? 0 : server.log.back().index, server.log.size() == 0 ? 0 : server.log.back().term},
            });
        }
    }
}

void update_server(Server& server, float dt) {
    switch (server.state) {
        case ServerState::Leader: {
            update_leader(server, dt);
        } break;
        case ServerState::Candidate: {
            update_candidate(server, dt);
        } break;
        case ServerState::Follower: {
            update_follower(server, dt);
        } break;
        default:
            assert(false);
    }
}

static Color server_draw_color(ServerState server_state) {
    switch (server_state) {
        case ServerState::Leader: {
            return SRCERY_RED;
        } break;
        case ServerState::Candidate: {
            return SRCERY_BLUE;
        } break;
        case ServerState::Follower: {
            return SRCERY_WHITE;
        } break;
        default:
            assert(false);
    }
}

int main() {
    std::vector<Server> servers(SERVER_COUNT);

    for (int i = 0; i < SERVER_COUNT; ++i) {
        servers[i].id = i + 1; // ids start at one
        servers[i].election_timer = 1.0f + ELECTION_TIMEOUT * (float)rand() / (float)RAND_MAX;
        for (int j = 0; j < SERVER_COUNT; ++j) {
            if (i != j) {
                servers[i].others.push_back(&servers[j]);
            }
        }

    }

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "radeau");
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();
        for (Server& server: servers) {
            update_server(server, dt);
        }
        BeginDrawing();
        ClearBackground(SRCERY_BLACK);

        for (int i = 0; i < SERVER_COUNT; ++i) {
            const Server& server = servers[i];
            int server_x = WINDOW_WIDTH * (i + 1) / (SERVER_COUNT + 1);
            DrawCircle(server_x, 50, 10.0f, server_draw_color(server.state));
            DrawText(TextFormat("%d", server.id), server_x, 80, 15, SRCERY_BRIGHTWHITE);
            DrawText(TextFormat("Term: %d", server.term), server_x, 100, 15, SRCERY_BRIGHTWHITE);
            DrawText(TextFormat("Voted for: %d", server.voted_for), server_x, 120, 15, SRCERY_BRIGHTWHITE);
            DrawText(TextFormat("Log Size: %d", server.log.size()), server_x, 140, 15, SRCERY_BRIGHTWHITE);
            DrawText(TextFormat("Commit Index: %d", server.commit_index), server_x, 160, 15, SRCERY_BRIGHTWHITE);
            DrawText(TextFormat("Last Applied: %d", server.last_applied), server_x, 180, 15, SRCERY_BRIGHTWHITE);
            DrawText(TextFormat("Election Timer: %.2f", server.election_timer), server_x, 200, 15, SRCERY_BRIGHTWHITE);
            DrawText(TextFormat("Heartbeat Timer: %.2f", server.heartbeat_timer), server_x, 220, 15, SRCERY_BRIGHTWHITE);
        }

        EndDrawing();
    }

    //while (true) {
    //    const float dt = 1.0f;
    //    for (Server& server: servers) {
    //        update_server(server, dt);
    //    }
    //}

    return 0;
}
