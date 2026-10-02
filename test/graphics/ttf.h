#include <cxxtest/TestSuite.h>
#include "common/config-manager.h"
#include "common/file.h"
#include "common/compression/unzip.h"
#include "graphics/font.h"
#include "graphics/fonts/ttf.h"
#include "graphics/managed_surface.h"
#include "test/system/null_osystem.h"

#if defined(USE_FREETYPE2) && NULL_OSYSTEM_IS_AVAILABLE
class TTFFontTestSuite : public CxxTest::TestSuite {
public:
	void setUp() {
		Common::install_null_g_system();
		ConfMan.setPath("extrapath", Common::Path("dists/engine-data"));
	}

	void tearDown() {
		ConfMan.removeKey("extrapath", Common::ConfigManager::kApplicationDomain);
		Graphics::shutdownTTF();
		Common::uninstall_null_g_system();
	}

	void testGlyphCompositionOnTransparentSurface() { checkComposition(0); }
	void testGlyphCompositionOnTranslucentSurface() { checkComposition(128); }
	void testGlyphCompositionOnOpaqueSurface() { checkComposition(255); }
	void testGlyphCompositionOnColorKey() { checkComposition(0, true); }

	void testWindows1252EuroGlyph() {
		char bytes[256];
		for (uint i = 0; i < 256; ++i)
			bytes[i] = (char)i;
		const Common::U32String unicode(bytes, 256, Common::kWindows1252);
		TS_ASSERT_EQUALS(unicode.size(), 256U);
		TS_ASSERT_EQUALS(unicode[0x80], 0x20ACU);
		uint32 mapping[256];
		for (uint i = 0; i < 256; ++i)
			mapping[i] = unicode[i];
		Graphics::Font *mapped = Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf", 18,
			Graphics::kTTFSizeModeCharacter, 0, 0, Graphics::kTTFRenderModeLight, mapping);
		Graphics::Font *plain = Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf", 18);
		TS_ASSERT(mapped);
		TS_ASSERT(plain);
		if (mapped && plain) {
			TS_ASSERT(mapped->getCharWidth(0x80) > 0);
			TS_ASSERT_EQUALS(mapped->getCharWidth(0x80), plain->getCharWidth(0x20AC));
			const Graphics::PixelFormat format(4, 8, 8, 8, 8, 16, 8, 0, 24);
			Graphics::ManagedSurface a(32, 32, format), b(32, 32, format);
			a.clear(0); b.clear(0);
			mapped->drawChar(&a, 0x80, 0, 0, 0xffffffff);
			plain->drawChar(&b, 0x20AC, 0, 0, 0xffffffff);
			for (int y = 0; y < 32; ++y)
				for (int x = 0; x < 32; ++x)
					TS_ASSERT_EQUALS(a.getPixel(x, y), b.getPixel(x, y));
		}
		delete mapped;
		delete plain;
	}

	void testFindFaceAfterMetadataProbe() {
		// Extract a redistributable fixture instead of depending on host fonts.
		Common::Archive *archive = Common::makeZipArchive(Common::FSNode("dists/engine-data/fonts.dat").createReadStream());
		TS_ASSERT(archive);
		if (!archive)
			return;
		Common::SeekableReadStream *input = archive->createReadStreamForMember("LiberationSans-Regular.ttf");
		TS_ASSERT(input);
		if (!input) { delete archive; return; }
		Common::DumpFile output;
		const Common::Path path("test/engine-data/ttf-test.ttf");
		TS_ASSERT(output.open(path));
		byte buffer[4096];
		while (!input->eos()) {
			uint32 count = input->read(buffer, sizeof(buffer));
			output.write(buffer, count);
		}
		output.close();
		delete input;
		delete archive;
		Common::Array<Common::Path> files;
		files.push_back(Common::Path("ttf-test.ttf"));
		Graphics::Font *font = Graphics::findTTFace(files, Common::U32String("Liberation Sans"), false, false, 18, 72, 72);
		TS_ASSERT(font);
		if (font) {
			TS_ASSERT(font->getCharWidth('W') > 0);
			delete font;
		}
	}

private:
	void checkComposition(byte backgroundAlpha, bool colorKey = false) {
		Graphics::Font *font = Graphics::loadTTFFontFromArchive("LiberationSans-Regular.ttf", 18);
		TS_ASSERT(font);
		if (!font)
			return;
		const Graphics::PixelFormat format(4, 8, 8, 8, 8, 16, 8, 0, 24);
		Graphics::ManagedSurface mask(48, 48, format), actual(48, 48, format);
		mask.clear(0);
		const uint32 background = format.ARGBToColor(colorKey ? 255 : backgroundAlpha, 40, 80, 120);
		actual.clear(background);
		if (colorKey)
			actual.setTransparentColor(background);
		const uint32 foreground = format.ARGBToColor(255, 210, 180, 75);
		font->drawAlphaChar(&mask, 'S', 2, 2, foreground);
		font->drawChar(&actual, 'S', 2, 2, foreground);

		uint edgePixels = 0;
		for (int y = 0; y < mask.h; ++y) {
			for (int x = 0; x < mask.w; ++x) {
				byte coverage, mr, mg, mb;
				format.colorToARGB(mask.getPixel(x, y), coverage, mr, mg, mb);
				if (!coverage)
					continue;
				if (coverage != 255)
					++edgePixels;
				byte a, r, g, b;
				format.colorToARGB(actual.getPixel(x, y), a, r, g, b);
				// Independent source-over oracle in integer alpha units. Use
				// the coverage mask, not any assumed FreeType glyph shape.
				const uint weight = coverage * 255 + backgroundAlpha * (255 - coverage);
				TS_ASSERT_DELTA(a, weight / 255, 1);
				TS_ASSERT_DELTA(r, (210U * coverage * 255 + 40U * backgroundAlpha * (255 - coverage)) / weight, 1);
				TS_ASSERT_DELTA(g, (180U * coverage * 255 + 80U * backgroundAlpha * (255 - coverage)) / weight, 1);
				TS_ASSERT_DELTA(b, (75U * coverage * 255 + 120U * backgroundAlpha * (255 - coverage)) / weight, 1);
			}
		}
		TS_ASSERT(edgePixels > 0);
		delete font;
	}
};
#endif
