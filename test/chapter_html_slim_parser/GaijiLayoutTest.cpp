#include <Epub.h>
#include <Epub/Page.h>
#include <Epub/parsers/ChapterHtmlSlimParser.h>
#include <GfxRenderer.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <tuple>
#include <vector>

namespace {
class GaijiLayoutTest : public ::testing::Test {
 protected:
  GfxRenderer renderer;
  std::vector<std::unique_ptr<Page>> pages;
  std::vector<uint32_t> offsets;

  bool parse(const std::string& body, bool vertical = false, uint16_t width = 96, uint16_t height = 160,
             uint8_t rendering = 0, bool css = true) {
    pages.clear();
    offsets.clear();
    auto epub = std::make_shared<Epub>();
    for (auto [name, w, h] :
         {std::tuple{"square.png", 128u, 128u}, {"tall.png", 128u, 256u}, {"wide.png", 256u, 128u}}) {
      std::vector<uint8_t> bytes = {0x89, 'P', 'N', 'G', 13, 10, 26, 10, 0, 0, 0, 13, 'I', 'H', 'D', 'R'};
      bytes.reserve(24);
      for (auto value : {w, h})
        for (int shift = 24; shift >= 0; shift -= 8) bytes.push_back(static_cast<uint8_t>(value >> shift));
      epub->entries.emplace(name, std::move(bytes));
    }
    const auto path =
        std::filesystem::temp_directory_path() /
        (std::string("crosspoint-gaiji-") + ::testing::UnitTest::GetInstance()->current_test_info()->name() + ".xhtml");
    const std::string filepath = path.string();
    {
      std::ofstream out(path);
      out << "<html><body>" << body << "</body></html>";
    }
    bool result;
    {
      ChapterHtmlSlimParser parser(
          epub, filepath, renderer, 0, 1.0f, false, 1, width, height, false, false,
          [&](std::unique_ptr<Page> page, auto, auto, uint32_t offset) {
            offsets.push_back(offset);
            pages.push_back(std::move(page));
          },
          css, "", "/tmp/crosspoint-gaiji-image-", rendering);
      parser.setParagraphIndentSpaces(0);
      parser.setWritingMode(vertical ? WritingMode::Vertical : WritingMode::Horizontal, 0);
      result = parser.parseAndBuildPages();
    }
    std::filesystem::remove(path);
    return result;
  }

  const PageLine* firstLine(const Page& page) {
    for (const auto& element : page.elements)
      if (element->getTag() == TAG_PageLine) return static_cast<const PageLine*>(element.get());
    return nullptr;
  }
  const PageImage* firstImage(const Page& page) {
    for (const auto& element : page.elements)
      if (element->getTag() == TAG_PageImage) return static_cast<const PageImage*>(element.get());
    return nullptr;
  }
};

TEST_F(GaijiLayoutTest, NamedVariantsStayInTheirSentenceInBothModes) {
  for (bool vertical : {false, true}) {
    for (auto [name, source, width, height] : {std::tuple{"gaiji", "square.png", 12, 12},
                                               {"gaiji-line", "tall.png", 12, 24},
                                               {"gaiji-wide", "wide.png", 24, 12}}) {
      SCOPED_TRACE(name);
      SCOPED_TRACE(vertical);
      ASSERT_TRUE(
          parse(std::string("<p>前<img class=\"extra ") + name + " extra\" src=\"" + source + "\"/>後</p>", vertical));
      ASSERT_EQ(pages.size(), 1u);
      const auto* line = firstLine(*pages[0]);
      const auto* image = firstImage(*pages[0]);
      ASSERT_NE(line, nullptr);
      ASSERT_NE(image, nullptr);
      ASSERT_EQ(line->getBlock()->wordCount(), 3);
      EXPECT_EQ(line->getBlock()->wordTextLen(1), 0);
      EXPECT_EQ(image->getImageBlock().getWidth(), width);
      EXPECT_EQ(image->getImageBlock().getHeight(), height);
      const auto* block = line->getBlock();
      if (vertical) {
        EXPECT_EQ(image->yPos, line->yPos + block->wordYpos(1));
        EXPECT_GE(line->yPos + block->wordYpos(2), image->yPos + height);
        EXPECT_GE(image->xPos, 0);
        EXPECT_LE(image->xPos + width, 96);
      } else {
        EXPECT_EQ(image->xPos, line->xPos + block->wordXpos(1));
        EXPECT_GE(line->xPos + block->wordXpos(2), image->xPos + width);
        EXPECT_EQ(image->yPos + height, line->yPos + renderer.getFontAscenderSize(0));
      }
    }
  }
}

TEST_F(GaijiLayoutTest, CssSizesAndExplicitDisplayDetermineLayout) {
  ASSERT_TRUE(parse("<p>前<img class=\"custom\" style=\"height:2em\" src=\"square.png\"/>後</p>"));
  ASSERT_EQ(pages.size(), 1u);
  ASSERT_EQ(firstLine(*pages[0])->getBlock()->wordCount(), 3);
  EXPECT_EQ(firstImage(*pages[0])->getImageBlock().getHeight(), 24);
  for (const char* attrs : {"class=\"gaiji\" style=\"display:block;width:1em\"", "class=\"not-gaiji\"", ""}) {
    ASSERT_TRUE(parse(std::string("<p>前<img ") + attrs + " src=\"square.png\"/>後</p>"));
    unsigned lines = 0;
    for (const auto& page : pages)
      for (const auto& element : page->elements) lines += element->getTag() == TAG_PageLine;
    EXPECT_EQ(lines, 2u);
  }
  ASSERT_TRUE(parse("<p>前<img class=\"gaiji\" style=\"display:none\" src=\"square.png\"/>後</p>"));
  ASSERT_EQ(pages.size(), 1u);
  EXPECT_FALSE(pages[0]->hasImages());
}

TEST_F(GaijiLayoutTest, ClassFallbackWorksWithoutEmbeddedCss) {
  ASSERT_TRUE(parse("<p>前<img class=\"gaiji\" src=\"square.png\"/>後</p>", false, 96, 160, 0, false));
  ASSERT_EQ(pages.size(), 1u);
  EXPECT_EQ(firstImage(*pages[0])->getImageBlock().getWidth(), 12);
}

TEST_F(GaijiLayoutTest, AltTextAndImageSettingsPreserveSentenceFlow) {
  for (uint8_t mode : {0, 1, 2}) {
    ASSERT_TRUE(parse("<p>前<img class=\"gaiji\" src=\"missing.svg\" alt=\"字\"/>後</p>", false, 96, 160, mode));
    ASSERT_EQ(pages.size(), 1u);
    EXPECT_FALSE(pages[0]->hasImages());
    std::string text;
    const auto* block = firstLine(*pages[0])->getBlock();
    for (uint16_t i = 0; i < block->wordCount(); ++i) text += block->wordText(i);
    EXPECT_EQ(text, mode == 2 ? "前後" : "前字後");
  }
  ASSERT_TRUE(parse("<p>前<img class=\"gaiji\" src=\"square.png\" alt=\"字\"/>後</p>", false, 96, 160, 1));
  EXPECT_FALSE(pages[0]->hasImages());
}

TEST_F(GaijiLayoutTest, AdjacentImagesAndSourceSpacesKeepTheirAdvances) {
  ASSERT_TRUE(parse("<p>前<img class=\"gaiji\" src=\"square.png\"/><img class=\"gaiji\" src=\"square.png\"/> 後</p>"));
  ASSERT_EQ(pages.size(), 1u);
  const auto* block = firstLine(*pages[0])->getBlock();
  ASSERT_EQ(block->wordCount(), 4);
  EXPECT_EQ(block->wordXpos(2) - block->wordXpos(1), 12);
  EXPECT_EQ(block->wordXpos(3) - block->wordXpos(2), 16);
}

TEST_F(GaijiLayoutTest, LargeImagesClampAndExpandLineOrColumnSpacing) {
  for (bool vertical : {false, true}) {
    ASSERT_TRUE(parse("<p>前<img class=\"gaiji\" style=\"width:20em;height:30em\" src=\"square.png\"/>後</p>", vertical,
                      48, 48));
    unsigned count = 0;
    for (const auto& page : pages) {
      for (const auto& element : page->elements) {
        if (element->getTag() != TAG_PageImage) continue;
        ++count;
        const auto* image = static_cast<const PageImage*>(element.get());
        EXPECT_GE(image->xPos, 0);
        EXPECT_GE(image->yPos, 0);
        EXPECT_LE(image->xPos + image->getImageBlock().getWidth(), 48);
        EXPECT_LE(image->yPos + image->getImageBlock().getHeight(), 48);
      }
    }
    EXPECT_EQ(count, 1u);
  }
}

TEST_F(GaijiLayoutTest, RubyAndInternalLinksIncludeTheImage) {
  for (bool vertical : {false, true}) {
    ASSERT_TRUE(parse(
        "<p><a href=\"#note\"><ruby>前<img class=\"gaiji-line\" src=\"tall.png\"/>後<rt>まえあと</rt></ruby></a></p>",
        vertical));
    ASSERT_EQ(pages.size(), 1u);
    ASSERT_TRUE(firstLine(*pages[0])->getBlock()->hasRuby());
    const auto* image = firstImage(*pages[0]);
    ASSERT_NE(image, nullptr);
    ASSERT_FALSE(pages[0]->links.empty());
    bool covered = false;
    for (const auto& link : pages[0]->links)
      covered |= link.x <= image->xPos && link.y <= image->yPos &&
                 link.x + link.width >= image->xPos + image->getImageBlock().getWidth() &&
                 link.y + link.height >= image->yPos + image->getImageBlock().getHeight();
    EXPECT_TRUE(covered);
  }
}

TEST_F(GaijiLayoutTest, SoftFlushAndPageBreaksRetainEveryImageAndSourceOffset) {
  for (bool vertical : {false, true}) {
    std::string body = "<p>";
    for (int i = 0; i < 180; ++i) body += "前<img class=\"gaiji\" src=\"square.png\"/>後。";
    body += "</p>";
    ASSERT_TRUE(parse(body, vertical, 48, 48));
    unsigned images = 0;
    std::string text;
    for (const auto& page : pages) {
      for (const auto& element : page->elements) {
        if (element->getTag() == TAG_PageImage) ++images;
        if (element->getTag() != TAG_PageLine) continue;
        const auto* block = static_cast<const PageLine&>(*element).getBlock();
        EXPECT_NE(std::string(block->wordText(0)), "。");
        for (uint16_t i = 0; i < block->wordCount(); ++i) text += block->wordText(i);
      }
    }
    EXPECT_EQ(images, 180u);
    std::string expected;
    for (int i = 0; i < 180; ++i) expected += "前後。";
    EXPECT_EQ(text, expected);
    EXPECT_EQ(offsets.front(), 0u);
    EXPECT_TRUE(std::is_sorted(offsets.begin(), offsets.end()));
    EXPECT_LT(offsets.back(), 540u);
  }
}

TEST_F(GaijiLayoutTest, RtlReorderingRetainsObjectIdentityAndDimensions) {
  ASSERT_TRUE(
      parse("<p dir=\"rtl\">אב<img class=\"gaiji\" src=\"square.png\"/>גד <img class=\"gaiji-wide\" "
            "src=\"wide.png\"/>הו</p>"));
  ASSERT_EQ(pages.size(), 1u);
  const auto* block = firstLine(*pages[0])->getBlock();
  ASSERT_EQ(block->getInlineImages().size(), 2);
  for (auto* chunk = block->getInlineImages().firstChunk(); chunk; chunk = chunk->next.get()) {
    for (size_t i = 0; i < chunk->count; ++i) {
      const auto& record = chunk->records[i];
      EXPECT_EQ(block->wordTextLen(record.wordIndex), 0);
      EXPECT_EQ(block->wordFlowExtent(renderer, 0, record.wordIndex), record.width);
    }
  }
}

TEST_F(GaijiLayoutTest, CachedImageDimensionsRoundTripAndRejectCorruptRecords) {
  ASSERT_TRUE(parse("<p>前<img class=\"gaiji-wide\" src=\"wide.png\"/>後</p>"));
  const auto* original = firstLine(*pages[0])->getBlock();
  const auto path = std::filesystem::temp_directory_path() / "crosspoint-gaiji-block.bin";
  {
    HalFile out;
    ASSERT_TRUE(out.open(path.c_str(), "wb"));
    ASSERT_TRUE(original->serialize(out));
  }
  {
    HalFile in;
    ASSERT_TRUE(in.open(path.c_str(), "rb"));
    auto cached = TextBlock::deserialize(in);
    ASSERT_NE(cached, nullptr);
    ASSERT_EQ(cached->getInlineImages().size(), 1);
    EXPECT_EQ(cached->wordFlowExtent(renderer, 0, 1), 24);
    EXPECT_EQ(cached->wordXpos(2), original->wordXpos(2));
  }
  std::ifstream input(path, std::ios::binary);
  const std::string valid{std::istreambuf_iterator<char>(input), {}};
  input.close();
  for (int field : {0, 1, 2}) {
    auto invalid = valid;
    const uint16_t bad = field == 0 ? UINT16_MAX : 0;
    memcpy(invalid.data() + invalid.size() - 6 + field * 2, &bad, sizeof(bad));
    {
      std::ofstream out(path, std::ios::binary);
      out.write(invalid.data(), invalid.size());
    }
    HalFile in;
    ASSERT_TRUE(in.open(path.c_str(), "rb"));
    EXPECT_EQ(TextBlock::deserialize(in), nullptr);
  }
  {
    std::ofstream out(path, std::ios::binary);
    out.write(valid.data(), valid.size() - 1);
  }
  {
    HalFile in;
    ASSERT_TRUE(in.open(path.c_str(), "rb"));
    EXPECT_EQ(TextBlock::deserialize(in), nullptr);
  }
  std::filesystem::remove(path);
}
TEST_F(GaijiLayoutTest, TokenSplitsAndTrackingKeepSparseImageIndices) {
  for (bool vertical : {false, true}) {
    ParsedText text(true);
    text.addWord("abcdef", EpdFontFamily::REGULAR);
    ASSERT_TRUE(text.addInlineImage(std::make_unique<ImageBlock>("image.png", "source.png", 12, 12), true, 6));
    text.addWord("後", EpdFontFamily::REGULAR, false, true, 6);
    unsigned images = 0;
    const auto inspect = [&](std::unique_ptr<TextBlock> block, uint32_t) {
      for (uint16_t i = 0; i < block->wordCount(); ++i) {
        if (const auto* image = block->getInlineImages().find(i)) {
          ++images;
          EXPECT_EQ(block->wordTextLen(i), 0);
          EXPECT_EQ(image->image->getImagePath(), "image.png");
        }
      }
    };
    if (vertical)
      text.layoutVerticalColumns(renderer, 0, 32, 0, inspect);
    else
      text.layoutAndExtractLines(renderer, 0, 32, inspect, true, 2);
    EXPECT_EQ(images, 1u);
  }
  ParsedText tracked(false, false, BlockStyle(), 0);
  tracked.addWord("前", EpdFontFamily::REGULAR);
  ASSERT_TRUE(tracked.addInlineImage(std::make_unique<ImageBlock>("image.png", "source.png", 12, 12), true));
  tracked.addWord("後", EpdFontFamily::REGULAR, false, true);
  tracked.layoutAndExtractLines(
      renderer, 0, 100,
      [&](std::unique_ptr<TextBlock> block, uint32_t) {
        ASSERT_EQ(block->wordCount(), 3);
        EXPECT_EQ(block->wordXpos(1), 10);
        EXPECT_EQ(block->wordXpos(2), 24);
      },
      true, 2);
}

TEST_F(GaijiLayoutTest, SparseChunksCompactAndReleaseConsumedObjects) {
  InlineImageStore store;
  for (size_t i = 0; i < 33; ++i) ASSERT_TRUE(store.append(i, 12, 12));
  store.consumePrefix(15);
  ASSERT_EQ(store.size(), 18);
  ASSERT_NE(store.firstChunk(), nullptr);
  EXPECT_EQ(store.firstChunk()->count, 16);
  ASSERT_NE(store.firstChunk()->next, nullptr);
  EXPECT_EQ(store.firstChunk()->next->count, 2);
  EXPECT_EQ(store.firstChunk()->next->next, nullptr);
  for (size_t i = 0; i < 18; ++i) EXPECT_NE(store.find(i), nullptr);
  store.insertWord(3);
  EXPECT_EQ(store.find(3), nullptr);
  EXPECT_NE(store.find(18), nullptr);
  store.consumePrefix(19);
  EXPECT_EQ(store.firstChunk(), nullptr);
}

}  // namespace

TEST_F(GaijiLayoutTest, CompressedRowsReserveTheFullImageHeight) {
  renderer.lineHeight = 8;
  ASSERT_TRUE(
      parse("<p>前<img class=\"gaiji-line\" src=\"tall.png\"/>後前<img class=\"gaiji-line\" src=\"tall.png\"/>後</p>",
            false, 32));
  const PageImage* previous = nullptr;
  unsigned count = 0;
  for (const auto& page : pages) {
    for (const auto& element : page->elements) {
      if (element->getTag() != TAG_PageImage) continue;
      const auto* image = static_cast<const PageImage*>(element.get());
      if (previous) EXPECT_GE(image->yPos, previous->yPos + previous->getImageBlock().getHeight());
      previous = image;
      ++count;
    }
  }
  EXPECT_EQ(count, 2u);
}
