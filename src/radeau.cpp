#include <iostream>
#include <vector>
#include <cassert>

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
    Failure,
    RequestVote,
    AppendEntries,
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
    const std::vector<Log>& entries;
    int leader_commit_index;
};

struct AppendEntriesResponse {
    int term;
    bool success;
};

struct RPC {
    RPCType type;
    union {
        RequestVote request_vote;
        AppendEntries append_entries;
    };
};

struct RPCResponse {
    RPCType type;
    union {
        RequestVoteResponse request_vote_response;
        AppendEntriesResponse append_entries_response;
    };
};

struct Server {
    int id;  // ids start at 0
    std::vector<Server*> others;
    ServerState state {ServerState::Follower};
    float last_heartbeat_from_leader {0.0f};
    
    // persistent storage
    int term {0};
    int voted_for; // 0 indicates no vote
    std::vector<Log> log;

    // volatile state
    size_t commit_index;

    // volatide leader-only state
};

#define SERVER_COUNT 5

#define ELECTION_TIMEOUT 3.0f

AppendEntriesResponse server_send_append_entries(Server& server, AppendEntries append_entries) {
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
    for (const Log& log: append_entries.entries) {
        server.log.push_back(log);
    }
    return AppendEntriesResponse{ server.term, true };
}

RequestVoteResponse server_send_request_vote(Server& server, RequestVote request_vote) {
    RequestVoteResponse response {};
    if (server.term > request_vote.term) {
        response.term = server.term;
        response.vote_granted = false;
    } else if ((server.voted_for == 0 || server.voted_for == request_vote.candidate_id) && (request_vote.last_log_term >= server.log.back().term) && (request_vote.last_log_index >= (server.log.size() + 1))) {
        response.term = server.term;
        response.vote_granted = true;
    } else {
        response.term = server.term;
        response.vote_granted = false;
    }
    return response;
}

RPCResponse server_send_rpc(Server& server, RPC rpc) {
    switch (rpc.type) {
        case RPCType::RequestVote: {
            RequestVoteResponse response = server_send_request_vote(server, rpc.request_vote);
            return RPCResponse {
                RPCType::RequestVote,
                response,
            };
        } break;
        case RPCType::AppendEntries: {
            AppendEntriesResponse append_entries_response = server_send_append_entries(server, rpc.append_entries);
            RPCResponse response;
            response.type = RPCType::RequestVote;
            response.append_entries_response = append_entries_response;
            return response;

        } break;
        default:
            assert(false);
    }
    return RPCResponse {
        RPCType::Failure,
    };
}

void update_leader(Server& server, float dt) {
}

void update_candidate(Server& server, float dt) {
}

void update_follower(Server& server, float dt) {
    server.last_heartbeat_from_leader += dt;
    if (server.last_heartbeat_from_leader >= ELECTION_TIMEOUT) {
        // start the election
        server.term++;
        server.voted_for = server.id;
        server.state = ServerState::Candidate;
        int vote_count = 1;
        for (Server* other: server.others) {
            RPCResponse response = server_send_rpc(*other, RPC {
                RPCType::RequestVote,
                RequestVote {server.term, server.id, server.log.size() + 1, server.log.back().term},
            });
            assert(response.type == RPCType::RequestVote);
            RequestVoteResponse request_vote_response = response.request_vote_response;
            if (request_vote_response.vote_granted) {
                assert(request_vote_response.term <= server.term);
                vote_count++;
            }
            if (vote_count * 2 >= SERVER_COUNT) {
                // majority of votes, become the leader
                server.state = ServerState::Leader;
            }
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

int main() {
    std::vector<Server> servers(SERVER_COUNT);

    for (int i = 0; i < SERVER_COUNT; ++i) {
        servers[i].id = i + 1; // ids start at one
        for (int j = 0; j < SERVER_COUNT; ++j) {
            if (i != j) {
                servers[i].others.push_back(&servers[j]);
            }
        }

    }

    while (true) {
        const float dt = 1.0f;
        for (Server& server: servers) {
            update_server(server, dt);
        }
    }

    std::cout << "hello\n";
    return 0;
}
