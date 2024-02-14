/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This file is part of Ramses Composer
 * (see https://github.com/bmwcarit/ramses-composer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include <gtest/gtest.h>

#include "RamsesBaseFixture.h"
#include "ramses_adaptor/TextureSamplerAdaptor.h"
#include "testing/TestUtil.h"
#include "user_types/Enumerations.h"

using user_types::ETextureFormat;

class TextureAdaptorFixture : public RamsesBaseFixture<> {
public:
	void checkTextureFormats(core::SEditorObject texture, const std::map<user_types::ETextureFormat, core::ErrorLevel>& formats) {
		for (const auto& [format, level] : formats) {
			commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(format));
			dispatch();
			if (level == core::ErrorLevel::NONE) {
				ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
			} else {
				ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
				ASSERT_EQ(commandInterface.errors().getError({texture, &user_types::Texture::uri_}).level(), level);
			}
		}
	}

};

TEST_F(TextureAdaptorFixture, textureFormat8BitPalette) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "text-back-palette.png").string());
	dispatch();

	checkTextureFormats(texture, {{ETextureFormat::R8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RG8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RGB8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RGBA8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RGB16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::SRGB8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::SRGB8_ALPHA8, core::ErrorLevel::INFORMATION}});
}

TEST_F(TextureAdaptorFixture, textureFormatR8) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_gray.png").string());
	dispatch();

	checkTextureFormats(texture, {{ETextureFormat::R8, core::ErrorLevel::NONE},
									 // Swizzling results in R8 ramses texture format, being compatible with GRAY image. No warnings expected.
									 {ETextureFormat::RG8, core::ErrorLevel::NONE},
									 {ETextureFormat::RGB8, core::ErrorLevel::NONE},
									 {ETextureFormat::RGBA8, core::ErrorLevel::NONE},
									 // Swizzling results in R16F ramses format. It is incompatible with 8 bit image.
									 {ETextureFormat::RGB16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA16F, core::ErrorLevel::ERROR},
									 // Swizzling results in R8 ramses texture format, being compatible with GRAY image. No warnings expected.
									 {ETextureFormat::SRGB8, core::ErrorLevel::NONE},
									 {ETextureFormat::SRGB8_ALPHA8, core::ErrorLevel::NONE}});
}

TEST_F(TextureAdaptorFixture, textureFormatRG16) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_gray_16f.png").string());
	dispatch();

	checkTextureFormats(texture, {{ETextureFormat::R8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RG8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGB8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGB16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::SRGB8, core::ErrorLevel::ERROR},
									 {ETextureFormat::SRGB8_ALPHA8, core::ErrorLevel::ERROR}});
}

TEST_F(TextureAdaptorFixture, textureFormatRG8) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_gray_alpha.png").string());
	dispatch();

	checkTextureFormats(texture, {{ETextureFormat::R8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RG8, core::ErrorLevel::NONE},
									 // Swizzling results in R8 ramses texture format, must be info same as above.
									 {ETextureFormat::RGB8, core::ErrorLevel::INFORMATION},
									 // Swizzling results in RG8 ramses texture format, compatible with image, no errors.
									 {ETextureFormat::RGBA8, core::ErrorLevel::NONE},
									 {ETextureFormat::RGB16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA16F, core::ErrorLevel::ERROR},
									 // Swizzling results in R8 ramses texture format, must be info same as above.
									 {ETextureFormat::SRGB8, core::ErrorLevel::INFORMATION},
									 // Swizzling results in RG8 ramses texture format, compatible with image, no errors.
									 {ETextureFormat::SRGB8_ALPHA8, core::ErrorLevel::NONE}});
}

TEST_F(TextureAdaptorFixture, textureFormatRGB8) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "text-back.png").string());
	dispatch();

	checkTextureFormats(texture, {{ETextureFormat::R8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RG8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RGB8, core::ErrorLevel::NONE},
									 // Swizzle format is RGB8, compatible with file, no issues.
									 {ETextureFormat::RGBA8, core::ErrorLevel::NONE},
									 {ETextureFormat::RGB16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::SRGB8, core::ErrorLevel::NONE},
									 // Swizzle format is RGB8, compatible with file, no issues.
									 {ETextureFormat::SRGB8_ALPHA8, core::ErrorLevel::NONE}});
}

TEST_F(TextureAdaptorFixture, textureFormatRGBA8) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512.png").string());
	dispatch();

	checkTextureFormats(texture, {{ETextureFormat::R8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RG8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RGB8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RGBA8, core::ErrorLevel::NONE},
									 {ETextureFormat::RGB16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA16F, core::ErrorLevel::ERROR},
									 {ETextureFormat::SRGB8, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::SRGB8_ALPHA8, core::ErrorLevel::NONE}});
}

TEST_F(TextureAdaptorFixture, textureFormatRGBA8Flipped) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512.png").string());
	commandInterface.set({texture, &user_types::Texture::flipTexture_}, true);
	dispatch();

	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RGBA8));
	ASSERT_NO_THROW(dispatch());

	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RGB8));
	ASSERT_NO_THROW(dispatch());

	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RG8));
	ASSERT_NO_THROW(dispatch());

	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::R8));
	ASSERT_NO_THROW(dispatch());

	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::SRGB8));
	ASSERT_NO_THROW(dispatch());

	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::SRGB8_ALPHA8));
	ASSERT_NO_THROW(dispatch());
}

TEST_F(TextureAdaptorFixture, textureFormatRGB16) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_16f_no_alpha.png").string());
	dispatch();

	checkTextureFormats(texture, {{ETextureFormat::R8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RG8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGB8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGB16F, core::ErrorLevel::NONE},
									 // Swizzled format RGB16F is compatible with image data. No warning.
									 {ETextureFormat::RGBA16F, core::ErrorLevel::NONE},
									 {ETextureFormat::SRGB8, core::ErrorLevel::ERROR},
									 {ETextureFormat::SRGB8_ALPHA8, core::ErrorLevel::ERROR}});
}

TEST_F(TextureAdaptorFixture, textureFormatRGBA16From16i) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_16i.png").string());
	dispatch();

	checkTextureFormats(texture, {{ETextureFormat::R8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RG8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGB8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGB16F, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RGBA16F, core::ErrorLevel::NONE},
									 {ETextureFormat::SRGB8, core::ErrorLevel::ERROR},
									 {ETextureFormat::SRGB8_ALPHA8, core::ErrorLevel::ERROR}});
}

TEST_F(TextureAdaptorFixture, textureFormatRGBA16From16f) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_16f.png").string());
	dispatch();

	checkTextureFormats(texture, {{ETextureFormat::R8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RG8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGB8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGBA8, core::ErrorLevel::ERROR},
									 {ETextureFormat::RGB16F, core::ErrorLevel::INFORMATION},
									 {ETextureFormat::RGBA16F, core::ErrorLevel::NONE},
									 {ETextureFormat::SRGB8, core::ErrorLevel::ERROR},
									 {ETextureFormat::SRGB8_ALPHA8, core::ErrorLevel::ERROR}});
}

TEST_F(TextureAdaptorFixture, textureFormatRGBA16Flipped) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_16f.png").string());
	commandInterface.set({texture, &user_types::Texture::flipTexture_}, true);
	dispatch();

	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RGBA16F));
	ASSERT_NO_THROW(dispatch());

	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RGB16F));
	ASSERT_NO_THROW(dispatch());
}

TEST_F(TextureAdaptorFixture, textureFormatChangeValidToInvalid) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_16f.png").string());
	dispatch();

	// RGBA16
	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RGBA16F));
	dispatch();
	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512.png").string());
	dispatch();
	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
	ASSERT_EQ(commandInterface.errors().getError({texture, &user_types::Texture::uri_}).level(), core::ErrorLevel::ERROR);

	// R8
	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::R8));
	dispatch();
	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
	ASSERT_EQ(commandInterface.errors().getError({texture, &user_types::Texture::uri_}).level(), core::ErrorLevel::INFORMATION);

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_16f.png").string());
	dispatch();
	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
	ASSERT_EQ(commandInterface.errors().getError({texture, &user_types::Texture::uri_}).level(), core::ErrorLevel::ERROR);

	// RGBA16
	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RGBA16F));
	dispatch();
	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
}

TEST_F(TextureAdaptorFixture, textureGenerationAtMultipleLevels) {
	auto texture = create<user_types::Texture>("Texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "blue_1024.png").string());

	dispatch();

	commandInterface.set({texture, &user_types::Texture::level2uri_}, (test_path() / "images" / "green_512.png").string());

	dispatch();

	commandInterface.set({texture, &user_types::Texture::level3uri_}, (test_path() / "images" / "yellow_256.png").string());

	dispatch();

	commandInterface.set({texture, &user_types::Texture::level4uri_}, (test_path() / "images" / "red_128.png").string());

	dispatch();

	auto textureStuff{select<ramses::TextureSampler>(*sceneContext.scene(), ramses::ERamsesObjectType::TextureSampler)};
	ASSERT_EQ(textureStuff.size(), 1);

	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 2);
	dispatch();
	textureStuff = select<ramses::TextureSampler>(*sceneContext.scene(), ramses::ERamsesObjectType::TextureSampler);
	ASSERT_EQ(textureStuff.size(), 1);

	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 3);
	dispatch();
	textureStuff = select<ramses::TextureSampler>(*sceneContext.scene(), ramses::ERamsesObjectType::TextureSampler);
	ASSERT_EQ(textureStuff.size(), 1);

	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 4);
	dispatch();
	textureStuff = select<ramses::TextureSampler>(*sceneContext.scene(), ramses::ERamsesObjectType::TextureSampler);
	ASSERT_EQ(textureStuff.size(), 1);
}

TEST_F(TextureAdaptorFixture, textureGenerationAtMultipleLevelsWithFlip) {
	auto texture = create<user_types::Texture>("Texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "blue_1024.png").string());
	dispatch();

	commandInterface.set({texture, &user_types::Texture::level2uri_}, (test_path() / "images" / "green_512.png").string());
	dispatch();

	commandInterface.set({texture, &user_types::Texture::level3uri_}, (test_path() / "images" / "yellow_256.png").string());
	dispatch();

	commandInterface.set({texture, &user_types::Texture::level4uri_}, (test_path() / "images" / "red_128.png").string());
	dispatch();

	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 4);
	dispatch();

	commandInterface.set({texture, &user_types::Texture::flipTexture_}, true);
	dispatch();
	auto textureStuff = select<ramses::TextureSampler>(*sceneContext.scene(), ramses::ERamsesObjectType::TextureSampler);
	ASSERT_EQ(textureStuff.size(), 1);
}

TEST_F(TextureAdaptorFixture, wrongMipMapLevelImageSizes) {
	auto texture = create<user_types::Texture>("Texture");

	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 4);
	dispatch();

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "blue_1024.png").string());
	dispatch();

	commandInterface.set({texture, &user_types::Texture::level2uri_}, (test_path() / "images" / "blue_1024.png").string());
	dispatch();

	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::level2uri_}));

	commandInterface.set({texture, &user_types::Texture::level3uri_}, (test_path() / "images" / "blue_1024.png").string());
	dispatch();

	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::level3uri_}));

	commandInterface.set({texture, &user_types::Texture::level4uri_}, (test_path() / "images" / "blue_1024.png").string());
	dispatch();

	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::level4uri_}));

	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 3);
	dispatch();

	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::level4uri_}));

	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 2);
	dispatch();

	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::level3uri_}));

	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 1);
	dispatch();

	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::level2uri_}));
}

TEST_F(TextureAdaptorFixture, ramsesAutoMipMapGenerationWarningPersistsAfterChangingURI) {
	auto texture = create<user_types::Texture>("Texture");

	commandInterface.set({texture, &user_types::Texture::generateMipmaps_}, true);
	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 2);
	dispatch();
	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::mipmapLevel_}));

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "blue_1024.png").string());

	dispatch();
	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::mipmapLevel_}));

	commandInterface.set({texture, &user_types::Texture::level2uri_}, (test_path() / "images" / "green_512.png").string());

	dispatch();
	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::mipmapLevel_}));
}

TEST_F(TextureAdaptorFixture, textureBitdepthDifferentInSameLevel) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_16f.png").string());
	dispatch();

	// RGBA16
	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RGBA16F));
	dispatch();
	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::textureFormat_}));

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512.png").string());
	dispatch();
	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
	ASSERT_EQ(commandInterface.errors().getError({texture, &user_types::Texture::uri_}).level(), core::ErrorLevel::ERROR);

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "green_512_16f.png").string());
	dispatch();
	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
}

TEST_F(TextureAdaptorFixture, textureBitdepthDifferentInOtherLevel) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "blue_1024_16i.png").string());
	dispatch();

	// RGBA16
	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RGBA16F));
	dispatch();
	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::textureFormat_}));

	commandInterface.set({texture, &user_types::Texture::level2uri_}, (test_path() / "images" / "green_512.png").string());
	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 2);
	dispatch();
	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::level2uri_}));
	ASSERT_EQ(commandInterface.errors().getError({texture, &user_types::Texture::level2uri_}).level(), core::ErrorLevel::ERROR);

	commandInterface.set({texture, &user_types::Texture::level2uri_}, (test_path() / "images" / "green_512_16i.png").string());
	dispatch();
	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::level2uri_}));
}

TEST_F(TextureAdaptorFixture, textureFormatDifferentInOtherLevel) {
	auto texture = create<user_types::Texture>("texture");

	commandInterface.set({texture, &user_types::Texture::uri_}, (test_path() / "images" / "blue_1024.png").string());
	dispatch();

	// RGBA8
	commandInterface.set({texture, &user_types::Texture::textureFormat_}, static_cast<int>(ETextureFormat::RGBA8));
	dispatch();

	// R8
	commandInterface.set({texture, &user_types::Texture::level2uri_}, (test_path() / "images" / "green_512_gray.png").string());
	commandInterface.set({texture, &user_types::Texture::mipmapLevel_}, 2);
	dispatch();

	// Swizzled format is R8, compatible with image. No issues.
	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::level2uri_}));

	// RGBA8
	commandInterface.set({texture, &user_types::Texture::level2uri_}, (test_path() / "images" / "green_512.png").string());
	dispatch();

	// Swizzled format is RGBA8, matching image format.
	ASSERT_FALSE(commandInterface.errors().hasError({texture, &user_types::Texture::level2uri_}));
}


TEST_F(TextureAdaptorFixture, level1UriWarning) {
	auto texture = create<user_types::Texture>("texture");

	dispatch();

	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
	ASSERT_EQ(commandInterface.errors().getError({texture, &user_types::Texture::uri_}).level(), core::ErrorLevel::WARNING);
}

TEST_F(TextureAdaptorFixture, gitLfsPlaceholderFileInUri) {
	std::string path = test_path().append("images/gitLfsPlaceholderFile.png").string();
	raco::createGitLfsPlaceholderFile(path);

	auto texture = create <user_types::Texture>("texture");
	commandInterface.set({texture, &user_types::Texture::uri_}, path);
	dispatch();
	ASSERT_TRUE(commandInterface.errors().hasError({texture, &user_types::Texture::uri_}));
	ASSERT_TRUE(commandInterface.errors().getError({texture, &user_types::Texture::uri_}).message().find("Git LFS Placeholder"));
}
