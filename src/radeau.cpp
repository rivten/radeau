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

    size_t initial_message_id;
};

struct AppendEntries {
    int term;
    int leader_id;
    int prev_log_index;
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
    size_t message_id;
    int sender_id;
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
    int send_to_id;
    RPC rpc;
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
    std::vector<int> next_index; // also contains oneself, but ignored
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

static void server_push_log(Server& server, int term, std::string log) {
    TraceLog(LOG_INFO, TextFormat("(server %i) pushing log [%i %s] at index %zu", server.id, server.term, log.c_str(), server.log.size()));
    assert(server.log.size() < LOG_MAX_SIZE);
    server.log.push_back(Log {
        term,
        log,
    });
}

int server_get_last_log_index(const Server& server) {
    return server.log.size();
}

AppendEntriesResponse answer_append_entries(Server& server, AppendEntries append_entries, size_t initial_message_id, int sender_id) {
    assert(server.state != ServerState::Leader);

    if (server.state == ServerState::Candidate) {
        server.state = ServerState::Follower;
    }
    server.voted_for = 0;
    server.election_timer = 1.0f + ELECTION_TIMEOUT * (float)rand() / (float)RAND_MAX;

    if (append_entries.entry_count == 0) {
        TraceLog(LOG_INFO, "(server %i) received AppendEntries heartbeat from server %i", server.id, sender_id, server.term, append_entries.term);
        return AppendEntriesResponse { server.term, true, initial_message_id };
    }

    if (append_entries.term < server.term) {
        TraceLog(LOG_INFO, "(server %i) received AppendEntries from server %i with old term (my term: %s, her term: %s)", server.id, sender_id, server.term, append_entries.term);
        return AppendEntriesResponse { server.term, false, initial_message_id };
    }

    //if (server.log.size() <= append_entries.prev_log_index - 1) {
    //    return AppendEntriesResponse { server.term, false, initial_message_id };
    //}
    //Log log = server.log[append_entries.prev_log_index - 1];
    //if (log.term != append_entries.prev_log_term) {
    //    return AppendEntriesResponse { server.term, false, initial_message_id };
    //}

    if (server_get_last_log_index(server) <= append_entries.prev_log_index) {
        TraceLog(LOG_INFO, "(server %i) received AppendEntries from server %i with log too big (my log index: %zu, her log index: %zu)", server.id, sender_id, server_get_last_log_index(server), append_entries.prev_log_index);
        return AppendEntriesResponse {server.term, false, initial_message_id};
    }
    Log log = server.log[append_entries.prev_log_index];
    if (log.term != append_entries.prev_log_term) {
        TraceLog(LOG_INFO, "(server %i) received AppendEntries from server %i with bad previous log term (my log term: %i, her log term: %i)", server.id, sender_id, log.term, append_entries.prev_log_term);
        return AppendEntriesResponse {server.term, false, initial_message_id};
    }
    for (size_t i = 0; i < append_entries.entry_count; ++i) {
        Log log_to_add = append_entries.entries[i];
        // checking existing log entry
        size_t log_index = append_entries.prev_log_index + 1 + i;
        if (log_index < server.log.size()) {
            Log existing_log = server.log[log_index];
            if (existing_log.term != log_to_add.term || existing_log.log != log_to_add.log) {
                // conflict ! removing everything that follow
                // TODO: we should assert that nothing is commited
                server.log.resize(log_index - 1);
                TraceLog(LOG_INFO, TextFormat("(server %i) Resized log size to %i", server.id, server.log.size()));
                break;
            }
        }
    }
    for (size_t i = 0; i < append_entries.entry_count; ++i) {
        Log log_to_add = append_entries.entries[i];
        size_t log_index = append_entries.prev_log_index + 1 + i;
        if (log_index < server.log.size()) {
            Log existing_log = server.log[log_index];
            if (existing_log.term == log_to_add.term && existing_log.log == log_to_add.log) {
                // TODO: log
                continue;
            }
        }
        server_push_log(server, append_entries.entries[i].term, append_entries.entries[i].log);
    }
    if (append_entries.leader_commit_index > server.commit_index) {
        if (append_entries.entry_count == 0) {
            server.commit_index = append_entries.leader_commit_index;
        } else {
            server.commit_index = std::min(append_entries.leader_commit_index, append_entries.entry_count);
        }
    }
    return AppendEntriesResponse{ server.term, true, initial_message_id };
}

RequestVoteResponse answer_request_vote(Server& server, RequestVote request_vote, size_t initial_message_id, int sender_id) {
    RequestVoteResponse response {};
    if (server.term > request_vote.term) {
        TraceLog(LOG_INFO, TextFormat("(server %i) received RequestVote from server %i with term too low (my term: %i, her term: %i)", server.id, sender_id, server.term, request_vote.term));
        response.term = server.term;
        response.vote_granted = false;
        response.initial_message_id = initial_message_id;
    } else if ((server.voted_for == 0 || server.voted_for == request_vote.candidate_id) && (server.log.size() == 0 || request_vote.last_log_term >= server.log.back().term) && (request_vote.last_log_index >= server.log.size())) {
        TraceLog(LOG_INFO, TextFormat("(server %i) received RequestVote from server %i. ok to vote for her", server.id, sender_id, server.term, request_vote.term));
        response.term = server.term;
        server.voted_for = request_vote.candidate_id;
        response.vote_granted = true;
        response.initial_message_id = initial_message_id;
    } else {
        TraceLog(LOG_INFO, TextFormat("(server %i) received RequestVote from server %i. log mismatch or voted for someone already", server.id, sender_id));
        response.term = server.term;
        response.vote_granted = false;
        response.initial_message_id = initial_message_id;
    }
    return response;
}

void server_send_rpc(Server& server, RPC rpc) {
    assert(rpc.message_id != 0);
    server.messages.push_back(rpc);
}

void process_request_vote(Server& server, RequestVote request_vote, size_t message_id, int sender_id) {
        if (server.term < request_vote.term) {
            TraceLog(LOG_INFO, TextFormat("(server %i) received RequestVote response from server %i. her term is higher than mine, will become follower", server.id, sender_id));
            server.term = request_vote.term;
            server.state = ServerState::Follower;
        }
        RequestVoteResponse request_vote_response = answer_request_vote(server, request_vote, message_id, sender_id);
        auto s = std::find_if(begin(server.others), end(server.others), [rv = request_vote](Server* s) {
            return s->id == rv.candidate_id;
        });
        RPC response;
        response.type = RPCType::RequestVoteResponse;
        response.sender_id = server.id;
        response.message_id = server.next_message_id;
        server.next_message_id++;
        response.request_vote_response = request_vote_response;
        server_send_rpc(**s, response);
}

void process_append_entries(Server& server, AppendEntries append_entries, size_t message_id, int sender_id) {
    if (server.term < append_entries.term) {
        TraceLog(LOG_INFO, TextFormat("(server %i) received RequestVote response from server %i. her term is higher than mine, will become follower", server.id, sender_id));
        server.term = append_entries.term;
        server.state = ServerState::Follower;
    }

    if (server.state == ServerState::Candidate) {
        server.state = ServerState::Follower;
    }

    AppendEntriesResponse append_entries_response = answer_append_entries(server, append_entries, message_id, sender_id);
    RPC response;
    response.type = RPCType::AppendEntriesResponse;
    response.sender_id = server.id;
    response.append_entries_response = append_entries_response;
    response.message_id = server.next_message_id;
    server.next_message_id++;
    auto s = std::find_if(begin(server.others), end(server.others), [sender_id](Server* s) {
        return s->id == sender_id;
    });
    server_send_rpc(**s, response);
}

void process_request_vote_response(Server& server, RequestVoteResponse request_vote_response, size_t message_id, int sender_id) {
    assert(server.state == ServerState::Candidate || server.state == ServerState::Leader);
    if (server.term < request_vote_response.term) {
        TraceLog(LOG_INFO, TextFormat("(server %i) received RequestVote response from server %i. her term is higher than mine, will become follower", server.id, sender_id));
        server.term = request_vote_response.term;
        server.state = ServerState::Follower;
    }

    if (request_vote_response.vote_granted) {
        TraceLog(LOG_INFO, TextFormat("(server %i) received RequestVote response from server %i got her vote", server.id, sender_id));
        assert(request_vote_response.term <= server.term);
        server.votes.insert(sender_id);
    }
    if (server.votes.size() * 2 > SERVER_COUNT) {
        // majority of votes, become the leader
        TraceLog(LOG_INFO, TextFormat("(server %i) elected leader", server.id));
        server.state = ServerState::Leader;
        server.votes.clear();
        server.heartbeat_timer = 0.0f;
        for (int i = 0; i < SERVER_COUNT; ++i) {
            server.next_index[i] = server.log.size();
            server.match_index[i] = 0;
        }
    }
}

void process_append_entries_response(Server& server, AppendEntriesResponse append_entries_response, size_t message_id, int sender_id) {
    assert(server.state == ServerState::Leader);
    assert(append_entries_response.initial_message_id != 0);
    auto unanswered_message = std::find_if(begin(server.unanswered_messages), end(server.unanswered_messages), [&append_entries_response, &sender_id](const UnansweredMessage& msg){return msg.send_to_id == sender_id && msg.id == append_entries_response.initial_message_id;});
    assert(unanswered_message != end(server.unanswered_messages));

    if (server.term < append_entries_response.term) {
        TraceLog(LOG_INFO, TextFormat("(server %i) received AppendEntries response from server %i. her term is higher than mine, will become follower", server.id, sender_id));
        server.term = append_entries_response.term;
        server.state = ServerState::Follower;
        goto cleanup;
    }

    if (unanswered_message->rpc.append_entries.entry_count != 0) {
        if (append_entries_response.success) {
            TraceLog(LOG_INFO, TextFormat("(server %i) received AppendEntries response from server %i. successfull. changing next index from %i to %i", server.id, sender_id, server.next_index[sender_id - 1], unanswered_message->rpc.append_entries.prev_log_index + unanswered_message->rpc.append_entries.entry_count + 1));
            server.next_index[sender_id - 1] = unanswered_message->rpc.append_entries.prev_log_index + unanswered_message->rpc.append_entries.entry_count + 1;
        } else {
            TraceLog(LOG_INFO, TextFormat("(server %i) received AppendEntries response from server %i. response was negative. downgrading next index from %i to %i", server.id, sender_id, server.next_index[sender_id - 1], server.next_index[sender_id - 1] - 1));
            server.next_index[sender_id - 1]--;
        }
    }

cleanup:
    server.unanswered_messages.erase(unanswered_message);
}

void process_rpc(Server& server, RPC rpc) {
    assert(rpc.message_id != 0);
    assert(rpc.sender_id != 0);
    switch (rpc.type) {
        case RPCType::RequestVote: {
            process_request_vote(server, rpc.request_vote, rpc.message_id, rpc.sender_id);
        } break;
        case RPCType::AppendEntries: {
            process_append_entries(server, rpc.append_entries, rpc.message_id, rpc.sender_id);
        } break;
        case RPCType::RequestVoteResponse: {
            process_request_vote_response(server, rpc.request_vote_response, rpc.message_id, rpc.sender_id);
        } break;
        case RPCType::AppendEntriesResponse: {
            process_append_entries_response(server, rpc.append_entries_response, rpc.message_id, rpc.sender_id);
        } break;
        default:
            assert(false);
    }
}

void update_leader(Server& server, float dt) {
    server.heartbeat_timer -= dt;

    if (server.heartbeat_timer <= 0.0f) {
        server.heartbeat_timer = 3.0f * (float)rand() / (float)RAND_MAX;
        TraceLog(LOG_INFO, TextFormat("(server %i) sending heartbeat message", server.id));
        for (Server* other: server.others) {
            AppendEntries append_entries = AppendEntries {
                server.term,
                server.id,
                0,
                0,
                nullptr,
                0,
                server.commit_index,
            };
            RPC rpc;
            rpc.type = RPCType::AppendEntries;
            rpc.message_id = server.next_message_id;
            server.next_message_id++;
            rpc.sender_id = server.id;
            rpc.append_entries = append_entries;
            server.unanswered_messages.push_back(UnansweredMessage { rpc.message_id, 0.0f, other->id, rpc });
            server_send_rpc(*other, rpc);
        }
    }

    if (server.log.size() != 0) {
        int last_log_index = server.log.size() - 1;
        for (Server* other: server.others) {
            int next_index = server.next_index[other->id - 1];
            //size_t match_index = server.match_index[other->id - 1];
            if (last_log_index >= next_index) {
                TraceLog(LOG_INFO, TextFormat("(server %i) server %i has a next index of %zi while my last log index is %zu, sending entries", server.id, other->id, next_index, last_log_index));
                //assert(next_index < server.log.size());
                //assert(next_index > 0);
                assert(server.log.size() > 0);
                AppendEntries append_entries = AppendEntries {
                    server.term,
                    server.id,
                    next_index - 1,
                    server.log[next_index - 1].term,
                    &server.log[next_index],
                    (size_t)(last_log_index - next_index + 1),
                    server.commit_index,
                };
                RPC rpc;
                rpc.type = RPCType::AppendEntries;
                rpc.append_entries = append_entries;
                rpc.message_id = server.next_message_id;
                server.next_message_id++;
                server.unanswered_messages.push_back(UnansweredMessage { rpc.message_id, 0.0f, other->id, rpc });
                rpc.sender_id = server.id;
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
        TraceLog(LOG_INFO, TextFormat("(server %i) election timeout, triggering election", server.id));
        server.term++;
        server.voted_for = server.id;
        server.state = ServerState::Candidate;
        server.election_timer = 1.0f + ELECTION_TIMEOUT * (float)rand() / (float)RAND_MAX;
        for (Server* other: server.others) {
            RPC rpc  = RPC{
                RPCType::RequestVote,
                server.next_message_id,
                server.id,
                RequestVote {server.term, server.id, server.log.size(), server.log.size() == 0 ? 0 : server.log.back().term},
            };
            server.next_message_id++;
            server.unanswered_messages.push_back(UnansweredMessage { rpc.message_id, 0.0f, other->id, rpc });
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
        assert(message.send_to_id != 0);
        assert(message.id != 0);
        message.time += dt;
    }

    server.unanswered_messages.remove_if([](const UnansweredMessage& message){return message.time >= MESSAGE_TIMEOUT;});

    for (RPC rpc: server.messages) {
        process_rpc(server, rpc);
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
                server_push_log(*leader, leader->term, "hello");
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
            DrawText(TextFormat("Next Message ID: %zu", server.next_message_id), server_x, 240, 15, SRCERY_BRIGHTWHITE);
            DrawText(TextFormat("Unanswered count: %d", server.unanswered_messages.size()), server_x, 260, 15, SRCERY_BRIGHTWHITE);
        }

        EndDrawing();
    }

    return 0;
}
