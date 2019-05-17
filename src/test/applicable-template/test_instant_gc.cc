/**
 * Copyright (c) 2018-2019 Mingxin Wang. All rights reserved.
 */

#include <cstdio>
#include <random>
#include <vector>

#include "./instant_gc.h"

struct Room {
  explicit Room(std::size_t id) : id_(id)
      { printf("Construct Room #%llu\n", id_); }
  ~Room() { printf("Destruct Room #%llu\n", id_); }

  const std::vector<Room*>& get_related_pointers() const { return doors_; }

  std::size_t id_;
  std::vector<Room*> doors_;
};

void ConnectRooms(Room* a, Room* b) {
  if (a != nullptr && b != nullptr) {
    a->doors_.push_back(b);
    b->doors_.push_back(a);
  }
}

Room* generate_dungeon(int N, double P) {
  std::mt19937 gen(std::random_device{}());
  std::uniform_real_distribution<double> dis;
  std::vector<std::vector<Room*>> rooms(N);
  std::size_t next_id = 1u;
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      rooms[i].push_back(dis(gen) < P ? new Room(next_id++) : nullptr);
    }
  }
  delete rooms[N / 2][N / 2];
  Room* result = rooms[N / 2][N / 2] = new Room(0u);
  for (int i = 0; i < N - 1; ++i) {
    for (int j = 0; j < N; ++j) {
      ConnectRooms(rooms[i][j], rooms[i + 1][j]);
    }
  }
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N - 1; ++j) {
      ConnectRooms(rooms[i][j], rooms[i][j + 1]);
    }
  }
  test::instant_gc(rooms, result);
  return result;
}

int main() {
  Room* room = generate_dungeon(5, 0.5);
  puts("The dungeon was generated");
  test::instant_gc(room);
}
