#include <iostream>
#include <vector>
#include <cassert>
#include <cstdlib>
#include <algorithm>
#include <list>
#include <unordered_set>

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

#define LOG_MAX_SIZE 1024*1024

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

    size_t initial_message_id  {0};
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

    size_t initial_message_id;
};

struct RPC {
    RPCType type;
    size_t message_id {0};
    int sender_id {0};
    union {
        RequestVote request_vote;
        RequestVoteResponse request_vote_response;
        AppendEntries append_entries;
        AppendEntriesResponse append_entries_response;
    };
};

#define MESSAGE_TIMEOUT 10.0f

struct UnansweredMessage
{
    size_t id;
    float time;
    int sender_id;
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
    std::vector<size_t> next_index; // also contains oneself, but ignored
    std::vector<size_t> match_index; // also contains oneself, but ignored

    // simulation state -- not part of raft
    float heartbeat_timer;
    std::list<RPC> messages;
    std::unordered_set<int> votes;
    size_t next_message_id {1};
    std::list<UnansweredMessage> unanswered_messages;
};

#define SERVER_COUNT 5

#define ELECTION_TIMEOUT 10.0f

AppendEntriesResponse answer_append_entries(Server& server, AppendEntries append_entries) {
    assert(server.state != ServerState::Leader);

    if (server.state == ServerState::Candidate) {
        server.state = ServerState::Follower;
    }
    server.voted_for = 0;
    server.election_timer = 1.0f + ELECTION_TIMEOUT * (float)rand() / (float)RAND_MAX;

    if (append_entries.term < server.term) {
        // todo: add message id
        return AppendEntriesResponse { server.term, false };
    }
    //if (server.log.size() <= append_entries.prev_log_index - 1) {
    //    return AppendEntriesResponse { server.term, false };
    //}
    //Log log = server.log[append_entries.prev_log_index - 1];
    //if (log.term != append_entries.prev_log_term) {
    //    return AppendEntriesResponse { server.term, false };
    //}
    if (server.log.size() > append_entries.prev_log_index - 1) {
        Log log = server.log[append_entries.prev_log_index - 1];
        if (log.term != append_entries.prev_log_term) {
            return AppendEntriesResponse {server.term, false};
        }
        if (log.term != append_entries.term) {
            server.log.resize(append_entries.prev_log_index - 2); // not sure about - 1 here
        }
    }
    for (size_t i = 0; i < append_entries.entry_count; ++i) {
        assert(server.log.size() < LOG_MAX_SIZE);
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

RequestVoteResponse answer_request_vote(Server& server, RequestVote request_vote, size_t message_id) {
    RequestVoteResponse response {};
    if (server.term > request_vote.term) {
        response.term = server.term;
        response.vote_granted = false;
        response.initial_message_id = message_id;
    } else if ((server.voted_for == 0 || server.voted_for == request_vote.candidate_id) && (server.log.size() == 0 || request_vote.last_log_term >= server.log.back().term) && (server.log.size() == 0 || request_vote.last_log_index >= server.log.back().index)) {
        response.term = server.term;
        server.voted_for = request_vote.candidate_id;
        response.vote_granted = true;
        response.initial_message_id = message_id;
    } else {
        response.term = server.term;
        response.vote_granted = false;
        response.initial_message_id = message_id;
    }
    return response;
}

void server_send_rpc(Server& server, RPC rpc) {
    if (rpc.type == RPCType::RequestVote || rpc.type == RPCType::AppendEntries) {
        assert(rpc.message_id != 0);
    }
    server.messages.push_back(rpc);
}

void answer_rpc(Server& server, RPC rpc) {
    assert(rpc.message_id != 0);
    assert(rpc.sender_id != 0);
    switch (rpc.type) {
        case RPCType::RequestVote: {
            if (server.term < rpc.request_vote.term) {
                server.term = rpc.request_vote.term;
                server.state = ServerState::Follower;
            }
            RequestVoteResponse request_vote_response = answer_request_vote(server, rpc.request_vote, rpc.message_id);
            auto s = std::find_if(begin(server.others), end(server.others), [rv = rpc.request_vote](Server* s) {
                return s->id == rv.candidate_id;
            });
            RPC response;
            response.type = RPCType::RequestVoteResponse;
            response.sender_id = server.id;
            response.message_id = server.next_message_id;
            server.next_message_id++;
            response.request_vote_response = request_vote_response;
            server_send_rpc(**s, response);
        } break;
        case RPCType::AppendEntries: {
            if (server.term < rpc.append_entries.term) {
                server.term = rpc.append_entries.term;
                server.state = ServerState::Follower;
            }

            if (server.state == ServerState::Candidate) {
                server.state = ServerState::Follower;
            }

            AppendEntriesResponse append_entries_response = answer_append_entries(server, rpc.append_entries);
            RPC response;
            response.type = RPCType::AppendEntriesResponse;
            response.sender_id = server.id;
            response.append_entries_response = append_entries_response;
            response.message_id = server.next_message_id;
            server.next_message_id++;
            auto s = std::find_if(begin(server.others), end(server.others), [rv = rpc.request_vote](Server* s) {
                return s->id == rv.candidate_id;
            });
            server_send_rpc(**s, response);

        } break;
        case RPCType::RequestVoteResponse: {
            assert(server.state == ServerState::Candidate || server.state == ServerState::Leader);
            if (server.term < rpc.request_vote_response.term) {
                server.term = rpc.request_vote_response.term;
                server.state = ServerState::Follower;
            }

            RequestVoteResponse request_vote_response = rpc.request_vote_response;
            if (request_vote_response.vote_granted) {
                assert(request_vote_response.term <= server.term);
                server.votes.insert(rpc.sender_id);
            }
            if (server.votes.size() * 2 > SERVER_COUNT) {
                // majority of votes, become the leader
                server.state = ServerState::Leader;
                server.votes.clear();
                server.heartbeat_timer = 0.0f;
                for (int i = 0; i < SERVER_COUNT; ++i) {
                    server.next_index[i] = server.log.size() == 0 ? 0 : server.log.back().index + 1;
                    server.match_index[i] = 0;
                }
            }
        } break;
        case RPCType::AppendEntriesResponse: {
            if (server.term < rpc.append_entries_response.term) {
                server.term = rpc.append_entries_response.term;
                server.state = ServerState::Follower;
            }
            assert(server.state == ServerState::Leader);
            assert(rpc.append_entries_response.initial_message_id != 0);
            auto unanswered_message = std::find_if(begin(server.unanswered_messages), end(server.unanswered_messages), [&rpc](const UnansweredMessage& msg){return msg.sender_id == rpc.sender_id && msg.id == rpc.append_entries_response.initial_message_id;});
            assert(unanswered_message != end(server.unanswered_messages));
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
            rpc.message_id = server.next_message_id;
            rpc.sender_id = server.id;
            server.next_message_id++;
            rpc.append_entries = append_entries;
            server.unanswered_messages.push_back(UnansweredMessage { rpc.message_id, 0.0f, other->id });
            server_send_rpc(*other, rpc);
        }
    }

    if (server.log.size() != 0) {
        size_t last_log_index = server.log.back().index;
        for (Server* other: server.others) {
            size_t next_index = server.next_index[other->id - 1];
            //size_t match_index = server.match_index[other->id - 1];
            if (last_log_index >= next_index) {
                assert(next_index < server.log.size());
                AppendEntries append_entries = AppendEntries {
                    server.term,
                    server.id,
                    server.log.back().index,
                    server.log.back().term,
                    &server.log[next_index],
                    last_log_index - next_index,
                    server.commit_index,
                };
                RPC rpc;
                rpc.type = RPCType::AppendEntries;
                rpc.append_entries = append_entries;
                rpc.message_id = server.next_message_id;
                rpc.sender_id = server.id;
                server.next_message_id++;
                server_send_rpc(*other, rpc);
            }
        }
    }
}

void update_candidate(Server& server, float dt) {
    server.election_timer -= dt;

    if (server.election_timer <= 0.0f) {
        server.state = ServerState::Follower;
    }
}

void update_follower(Server& server, float dt) {
    server.election_timer -= dt;

    if (server.election_timer <= 0.0f) {
        // start the election
        server.term++;
        server.voted_for = server.id;
        server.state = ServerState::Candidate;
        server.election_timer = 1.0f + ELECTION_TIMEOUT * (float)rand() / (float)RAND_MAX;
        for (Server* other: server.others) {
            RPC rpc  = RPC{
                RPCType::RequestVote,
                server.next_message_id,
                server.id,
                RequestVote {server.term, server.id, server.log.size() == 0 ? 0 : server.log.back().index, server.log.size() == 0 ? 0 : server.log.back().term},
            };
            server.next_message_id++;
            server.unanswered_messages.push_back(UnansweredMessage { rpc.message_id, 0.0f, other->id });
            server_send_rpc(*other, rpc);
        }
    }
}

void update_server(Server& server, float dt) {
    if(server.commit_index > server.last_applied) {
        server.last_applied++;
        // apply server.log[last_applied]
    }


    for (UnansweredMessage& message: server.unanswered_messages) {
        assert(message.id != 0);
        message.time += dt;
    }

    server.unanswered_messages.remove_if([](const UnansweredMessage& message){return message.time >= MESSAGE_TIMEOUT;});

    for (RPC rpc: server.messages) {
        answer_rpc(server, rpc);
    }
    server.messages.clear();

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
    return SRCERY_BLACK;
}

#define PUSH_LOG_TIMER 10.0f

static void server_push_log(Server& server, std::string log) {
    assert(server.state == ServerState::Leader);
    assert(server.log.size() < LOG_MAX_SIZE);
    server.log.push_back(Log {
        server.term,
        server.log.size() == 0 ? 1 : server.log.back().index + 1,
        log,
    });
}

int main() {
    std::vector<Server> servers(SERVER_COUNT);

    for (int i = 0; i < SERVER_COUNT; ++i) {
        servers[i].id = i + 1; // ids start at one
        servers[i].election_timer = 1.0f + ELECTION_TIMEOUT * (float)rand() / (float)RAND_MAX;
        servers[i].log.reserve(LOG_MAX_SIZE);
        for (int j = 0; j < SERVER_COUNT; ++j) {
            if (i != j) {
                servers[i].others.push_back(&servers[j]);
            }
            servers[i].next_index.push_back(0);
            servers[i].match_index.push_back(0);
        }

    }

    float client_push_log_timer = PUSH_LOG_TIMER * (float)rand() / (float)RAND_MAX;

    InitWindow(WINDOW_WIDTH, WINDOW_HEIGHT, "radeau");
    while (!WindowShouldClose()) {
        float dt = GetFrameTime();

        client_push_log_timer -= dt;
        if (client_push_log_timer <= 0.0f) {
            client_push_log_timer = PUSH_LOG_TIMER * (float)rand() / (float)RAND_MAX;
            auto leader = std::find_if(begin(servers), end(servers), [](Server& s) {
                // TODO: isn't the real leader the one with max term in case of two leaders ?
                return s.state == ServerState::Leader;
            });
            if (leader != end(servers)) {
                server_push_log(*leader, "hello");
            }
        }

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

    return 0;
}
