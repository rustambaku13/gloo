/**
 * Copyright (c) 2018-present, Facebook, Inc.
 * All rights reserved.
 *
 * This source code is licensed under the BSD-style license found in the
 * LICENSE file in the root directory of this source tree. An additional grant
 * of patent rights can be found in the PATENTS file in the same directory.
 */

#include "gloo/test/base_test.h"

#include <unordered_set>

#include "gloo/transport/tcp/unbound_buffer.h"

namespace gloo {
namespace test {
namespace {

// Test parameterization (context size, buffer size).
using Param = std::tuple<int, int>;

// Test fixture.
class SendRecvTest : public BaseTest,
                     public ::testing::WithParamInterface<Param> {};

TEST_P(SendRecvTest, AllToAll) {
  auto contextSize = std::get<0>(GetParam());
  spawn(contextSize, [&](std::shared_ptr<Context> context) {
      using buffer_ptr = std::unique_ptr<::gloo::transport::UnboundBuffer>;
      std::vector<int> input(contextSize);
      std::vector<int> output(contextSize);
      std::vector<buffer_ptr> inputBuffers(contextSize);
      std::vector<buffer_ptr> outputBuffers(contextSize);

      // Initialize
      for (auto i = 0; i < context->size; i++) {
        input[i] = context->rank;
        output[i] = -1;
        inputBuffers[i] =
          context->createUnboundBuffer(&input[i], sizeof(input[i]));
        outputBuffers[i] =
          context->createUnboundBuffer(&output[i], sizeof(output[i]));
      }

      // Send a message with the local rank to every other rank
      for (auto i = 0; i < context->size; i++) {
        if (i == context->rank) {
          continue;
        }
        inputBuffers[i]->send(i, context->rank);
      }

      // Receive message from every other rank
      for (auto i = 0; i < context->size; i++) {
        if (i == context->rank) {
          continue;
        }
        outputBuffers[i]->recv(i, i);
      }

      // Wait for send and recv to complete
      for (auto i = 0; i < context->size; i++) {
        if (i == context->rank) {
          continue;
        }
        inputBuffers[i]->waitSend();
        outputBuffers[i]->waitRecv();
      }

      // Verify output
      for (auto i = 0; i < context->size; i++) {
        if (i == context->rank) {
          continue;
        }
        ASSERT_EQ(i, output[i]) << "Mismatch at index " << i;
      }
    });
}

TEST_P(SendRecvTest, AllToAllOffset) {
  auto contextSize = std::get<0>(GetParam());
  spawn(contextSize, [&](std::shared_ptr<Context> context) {
      auto elementSize = sizeof(int);
      std::vector<int> input(contextSize);
      std::vector<int> output(contextSize);
      auto inputBuffer = context->createUnboundBuffer(
          input.data(), input.size() * elementSize);
      auto outputBuffer = context->createUnboundBuffer(
          output.data(), output.size() * elementSize);

      // Initialize
      for (auto i = 0; i < context->size; i++) {
        input[i] = i;
        output[i] = -1;
      }

      // Send a message with the local rank to every other rank
      for (auto i = 0; i < context->size; i++) {
        if (i == context->rank) {
          continue;
        }
        inputBuffer->send(i, 0, context->rank * elementSize, elementSize);
      }

      // Receive message from every other rank
      for (auto i = 0; i < context->size; i++) {
        if (i == context->rank) {
          continue;
        }
        outputBuffer->recv(i, 0, i * elementSize, elementSize);
      }

      // Wait for send and recv to complete
      for (auto i = 0; i < context->size; i++) {
        if (i == context->rank) {
          continue;
        }
        inputBuffer->waitSend();
        outputBuffer->waitRecv();
      }

      // Verify output
      for (auto i = 0; i < context->size; i++) {
        if (i == context->rank) {
          continue;
        }
        ASSERT_EQ(i, output[i]) << "Mismatch at index " << i;
      }
    });
}

TEST_P(SendRecvTest, RecvFromAny) {
  auto contextSize = std::get<0>(GetParam());
  spawn(contextSize, [&](std::shared_ptr<Context> context) {
      constexpr uint64_t slot = 0x1337;
      if (context->rank == 0) {
        std::unordered_set<int> outputData;
        std::unordered_set<int> outputRanks;
        int tmp;
        auto buf = context->createUnboundBuffer(&tmp, sizeof(tmp));

        // Compile vector of ranks to receive from
        std::vector<int> ranks;
        for (auto i = 1; i < context->size; i++) {
          ranks.push_back(i);
        }

        // Receive from N-1 peers
        for (auto i = 1; i < context->size; i++) {
          int srcRank = -1;
          buf->recv(ranks, slot);
          buf->waitRecv(&srcRank);
          outputData.insert(tmp);
          outputRanks.insert(srcRank);
        }

        // Verify result
        for (auto i = 1; i < context->size; i++) {
          ASSERT_EQ(1, outputData.count(i)) << "Missing output " << i;
          ASSERT_EQ(1, outputRanks.count(i)) << "Missing rank " << i;
        }
      } else {
        // Send to rank 0
        int tmp = context->rank;
        auto buf = context->createUnboundBuffer(&tmp, sizeof(tmp));
        buf->send(0, slot);
        buf->waitSend();
      }
    });
}

TEST_P(SendRecvTest, RecvFromAnyOffset) {
  auto contextSize = std::get<0>(GetParam());
  spawn(contextSize, [&](std::shared_ptr<Context> context) {
      auto elementSize = sizeof(int);
      constexpr uint64_t slot = 0x1337;
      if (context->rank == 0) {
        std::unordered_set<int> outputData;
        std::unordered_set<int> outputRanks;
        std::array<int, 2> tmp;
        auto buf =
            context->createUnboundBuffer(tmp.data(), tmp.size() * elementSize);

        // Compile vector of ranks to receive from
        std::vector<int> ranks;
        for (auto i = 1; i < context->size; i++) {
          ranks.push_back(i);
        }

        // Receive from N-1 peers
        for (auto i = 1; i < context->size; i++) {
          int srcRank = -1;
          buf->recv(ranks, slot, (i % tmp.size()) * elementSize, elementSize);
          buf->waitRecv(&srcRank);
          outputData.insert(tmp[i % tmp.size()]);
          outputRanks.insert(srcRank);
        }

        // Verify result
        for (auto i = 1; i < context->size; i++) {
          ASSERT_EQ(1, outputData.count(i)) << "Missing output " << i;
          ASSERT_EQ(1, outputRanks.count(i)) << "Missing rank " << i;
        }
      } else {
        // Send to rank 0
        int tmp = context->rank;
        auto buf = context->createUnboundBuffer(&tmp, sizeof(tmp));
        buf->send(0, slot);
        buf->waitSend();
      }
    });
}

TEST_P(SendRecvTest, RecvFromAnyPipeline) {
  auto contextSize = std::get<0>(GetParam());
  spawn(contextSize, [&](std::shared_ptr<Context> context) {
      constexpr uint64_t slot = 0x1337;

      if (context->rank == 0) {
        std::vector<int> output;
        std::array<int, 2> tmp;
        auto buf0 = context->createUnboundBuffer(&tmp[0], sizeof(tmp[0]));
        auto buf1 = context->createUnboundBuffer(&tmp[1], sizeof(tmp[1]));

        // Compile vector of ranks to receive from
        std::vector<int> ranks;
        for (auto i = 0; i < context->size; i++) {
          if (i == context->rank) {
            continue;
          }
          ranks.push_back(i);
        }

        // Receive twice per peer
        for (auto i = 0; i < context->size - 1; i++) {
          buf0->recv(ranks, slot);
          buf1->recv(ranks, slot);
          buf0->waitRecv();
          buf1->waitRecv();
          output.push_back(tmp[0]);
          output.push_back(tmp[1]);
        }

        // Verify output
        std::sort(output.begin(), output.end());
        for (auto i = 1; i < context->size; i++) {
          ASSERT_EQ(i, output[(i-1) * 2 + 0]) << "Mismatch at " << i;
          ASSERT_EQ(i, output[(i-1) * 2 + 1]) << "Mismatch at " << i;
        }
      } else {
        // Send twice to rank 0 on the same slot
        std::array<int, 2> tmp;
        tmp[0] = context->rank;
        tmp[1] = context->rank;
        auto buf0 = context->createUnboundBuffer(&tmp[0], sizeof(tmp[0]));
        auto buf1 = context->createUnboundBuffer(&tmp[1], sizeof(tmp[1]));
        buf0->send(0, slot);
        buf1->send(0, slot);
        buf0->waitSend();
        buf1->waitSend();
      }
    });
}

INSTANTIATE_TEST_CASE_P(
    SendRecvDefault,
    SendRecvTest,
    ::testing::Combine(
        ::testing::Values(2, 3, 4, 5, 6, 7, 8),
        ::testing::Values(1)));


} // namespace
} // namespace test
} // namespace gloo
