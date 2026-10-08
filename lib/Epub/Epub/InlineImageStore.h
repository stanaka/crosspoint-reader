#pragma once

#include <Memory.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

#include "blocks/ImageBlock.h"

// Sparse layout records. Ownership moves to PageImage after a line is placed;
// dimensions remain available in cached TextBlocks for ruby and hit testing.
class InlineImageStore {
 public:
  struct Record {
    size_t wordIndex = 0;
    uint16_t width = 0;
    uint16_t height = 0;
    std::unique_ptr<ImageBlock> image;
  };
  static constexpr size_t CHUNK_CAPACITY = 16;
  struct Chunk {
    Record records[CHUNK_CAPACITY];
    size_t count = 0;
    std::unique_ptr<Chunk> next;
  };

 private:
  std::unique_ptr<Chunk> first;

 public:
  ~InlineImageStore() {
    // Detach each link before destruction; even a corrupt dense cache cannot recurse on the task stack.
    while (first) {
      auto retired = std::move(first);
      first = std::move(retired->next);
    }
  }
  Chunk* firstChunk() { return first.get(); }
  const Chunk* firstChunk() const { return first.get(); }
  bool append(size_t wordIndex, uint16_t width, uint16_t height, std::unique_ptr<ImageBlock> image = {}) {
    auto* slot = &first;
    while (*slot && (*slot)->count == CHUNK_CAPACITY) slot = &(*slot)->next;
    if (!*slot) {
      // sizeof(Chunk) per 16 images; records outlive callbacks and cannot use stack storage.
      *slot = makeUniqueNoThrow<Chunk>();
      if (!*slot) return false;
    }
    auto& record = (*slot)->records[(*slot)->count++];
    record.wordIndex = wordIndex;
    record.width = width;
    record.height = height;
    record.image = std::move(image);
    return true;
  }
  const Record* find(size_t wordIndex) const {
    for (auto* chunk = first.get(); chunk; chunk = chunk->next.get()) {
      for (size_t i = 0; i < chunk->count; ++i) {
        if (chunk->records[i].wordIndex == wordIndex) return &chunk->records[i];
      }
    }
    return nullptr;
  }
  Record* find(size_t wordIndex) { return const_cast<Record*>(std::as_const(*this).find(wordIndex)); }
  void insertWord(size_t index) {
    for (auto* chunk = first.get(); chunk; chunk = chunk->next.get()) {
      for (size_t i = 0; i < chunk->count; ++i) {
        if (chunk->records[i].wordIndex >= index) ++chunk->records[i].wordIndex;
      }
    }
  }
  void consumePrefix(size_t count) {
    auto* slot = &first;
    while (*slot) {
      auto& chunk = **slot;
      size_t retained = 0;
      for (size_t i = 0; i < chunk.count; ++i) {
        auto& record = chunk.records[i];
        if (record.wordIndex < count) {
          record.image.reset();
          continue;
        }
        record.wordIndex -= count;
        if (retained != i) chunk.records[retained] = std::move(record);
        ++retained;
      }
      chunk.count = retained;
      if (!retained) {
        auto retired = std::move(*slot);
        *slot = std::move(retired->next);
      } else {
        slot = &chunk.next;
      }
    }
    for (auto* chunk = first.get(); chunk && chunk->next; chunk = chunk->next.get()) {
      while (chunk->count < CHUNK_CAPACITY && chunk->next) {
        auto& next = *chunk->next;
        const size_t take = std::min(CHUNK_CAPACITY - chunk->count, next.count);
        for (size_t i = 0; i < take; ++i) chunk->records[chunk->count++] = std::move(next.records[i]);
        for (size_t i = take; i < next.count; ++i) next.records[i - take] = std::move(next.records[i]);
        next.count -= take;
        if (!next.count) {
          auto retired = std::move(chunk->next);
          chunk->next = std::move(retired->next);
        }
      }
    }
  }
  uint16_t size() const {
    size_t count = 0;
    for (auto* chunk = first.get(); chunk; chunk = chunk->next.get()) count += chunk->count;
    return static_cast<uint16_t>(count);
  }
};
