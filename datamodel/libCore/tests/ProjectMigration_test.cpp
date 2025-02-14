/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This file is part of Ramses Composer
 * (see https://github.com/bmwcarit/ramses-composer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "core/Queries.h"

#include "core/DynamicEditorObject.h"
#include "core/ExternalReferenceAnnotation.h"
#include "core/ProjectMigration.h"
#include "core/ProjectMigrationToV23.h"
#include "core/ProxyObjectFactory.h"
#include "utils/u8path.h"

#include "application/RaCoApplication.h"
#include "application/RaCoProject.h"

#include "core/Link.h"
#include "core/PathManager.h"
#include "core/SerializationKeys.h"

#include "ramses_adaptor/SceneBackend.h"

#include "ramses_base/BaseEngineBackend.h"

#include "user_types/Animation.h"
#include "user_types/CubeMap.h"
#include "user_types/Enumerations.h"
#include "user_types/Material.h"
#include "user_types/MeshNode.h"
#include "user_types/OrthographicCamera.h"
#include "user_types/PerspectiveCamera.h"
#include "user_types/RenderBuffer.h"
#include "user_types/RenderPass.h"
#include "user_types/Texture.h"

#include "testing/TestEnvironmentCore.h"
#include "testing/TestUtil.h"

#include <gtest/gtest.h>

constexpr bool GENERATE_DIFF{false};

using namespace raco::core;

const char testName_old[] = "Test";
const char testName_new[] = "Test";

static_assert(!std::is_same<serialization::proxy::Proxy<testName_old>, serialization::proxy::Proxy<testName_new>>::value);

struct MigrationTest : public TestEnvironmentCore {
	ramses_base::HeadlessEngineBackend backend;
	raco::application::RaCoApplication application{backend};

	// Check if the property types coming out of the migration code agree with the types
	// in the current version of the user types.
	// Failure indicates missing migration code.
	void checkPropertyTypes(const serialization::ProjectDeserializationInfoIR& deserializedIR) {
		auto userTypesPropMap = serialization::makeUserTypePropertyMap();

		for (const auto obj : deserializedIR.objects) {
			const auto& typesMap = userTypesPropMap.at(obj->getTypeDescription().typeName);

			for (size_t i = 0; i < obj->size(); i++) {
				auto propName = obj->name(i);
				auto it = typesMap.find(propName);
				ASSERT_TRUE(it != typesMap.end());
				EXPECT_EQ(it->second, obj->get(i)->typeName()) << fmt::format("property name: '{}'", propName);
			}
		}
	}

	std::unique_ptr<raco::application::RaCoProject> loadAndCheckJson(QString filename, int* outFileVersion = nullptr) {
		QFile file{filename};
		EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
		auto document{QJsonDocument::fromJson(file.readAll())};
		file.close();
		auto fileVersion{serialization::deserializeFileVersion(document)};
		EXPECT_TRUE(fileVersion <= serialization::RAMSES_PROJECT_FILE_VERSION);
		if (outFileVersion) {
			*outFileVersion = fileVersion;
		}

		// Perform deserialization to IR and migration by hand to check output of migration code:
		auto deserializedIR{serialization::deserializeProjectToIR(document, filename.toStdString())};
		auto& factory{serialization::proxy::ProxyObjectFactory::getInstance()};
		serialization::migrateProject(deserializedIR, factory);
		checkPropertyTypes(deserializedIR);

		LoadContext loadContext;
		auto racoproject = raco::application::RaCoProject::loadFromFile(filename, &application, loadContext);
		EXPECT_TRUE(racoproject != nullptr);

		return racoproject;
	}
};

TEST_F(MigrationTest, migrate_from_V1) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V1.rca").string()));

	ASSERT_EQ(racoproject->project()->settings()->sceneId_.asInt(), 123);
	ASSERT_NE(racoproject->project()->settings()->objectID(), "b5535e97-4e60-4d72-99a9-b137b2ed52a5");	// this was the magic hardcoded ID originally used by the migration code.
}

TEST_F(MigrationTest, migrate_from_V9) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V9.rca").string()));

	auto p = std::dynamic_pointer_cast<user_types::PerspectiveCamera>(core::Queries::findByName(racoproject->project()->instances(), "PerspectiveCamera"));
	ASSERT_EQ(p->viewport_->offsetX_.asInt(), 1);
	ASSERT_EQ(p->viewport_->offsetY_.asInt(), 1);
	ASSERT_EQ(p->viewport_->width_.asInt(), 1441);
	ASSERT_EQ(p->viewport_->height_.asInt(), 721);

	auto o = std::dynamic_pointer_cast<user_types::OrthographicCamera>(core::Queries::findByName(racoproject->project()->instances(), "OrthographicCamera"));
	ASSERT_EQ(o->viewport_->offsetX_.asInt(), 2);
	ASSERT_EQ(o->viewport_->offsetY_.asInt(), 2);
	ASSERT_EQ(o->viewport_->width_.asInt(), 1442);
	ASSERT_EQ(o->viewport_->height_.asInt(), 722);
}

TEST_F(MigrationTest, migrate_from_V10) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V10.rca").string()));

	auto meshnode = core::Queries::findByName(racoproject->project()->instances(), "MeshNode")->as<user_types::MeshNode>();

	ASSERT_TRUE(meshnode != nullptr);
	auto options = meshnode->getMaterialOptionsHandle(0);
	ASSERT_TRUE(options);
	ASSERT_FALSE(options.hasProperty("depthfunction"));
	ASSERT_TRUE(options.hasProperty("depthFunction"));
	ASSERT_EQ(options.get("depthFunction").asInt(), 1);

	ASSERT_TRUE(meshnode->getMaterialPrivateHandle(0));
	ASSERT_TRUE(meshnode->getMaterialPrivateHandle(0).asBool());

	auto material = core::Queries::findByName(racoproject->project()->instances(), "Material")->as<user_types::Material>();

	ASSERT_TRUE(material != nullptr);
	ASSERT_TRUE(material->uniforms_->size() > 0);
	for (size_t i = 0; i < material->uniforms_->size(); i++) {
		auto engineType = material->uniforms_->get(i)->query<user_types::EngineTypeAnnotation>()->type();
		bool hasLinkAnno = material->uniforms_->get(i)->query<core::LinkEndAnnotation>() != nullptr;
		ASSERT_TRUE((core::PropertyInterface::primitiveType(engineType) != data_storage::PrimitiveType::Ref) == hasLinkAnno);
	}

	ValueHandle uniforms{material, &user_types::Material::uniforms_};
	ASSERT_EQ(uniforms.get("scalar").asDouble(), 42.0);
	ASSERT_EQ(uniforms.get("count_").asInt(), 42);
	ASSERT_EQ(*uniforms.get("vec").asVec3f().x, 0.1);
	ASSERT_EQ(*uniforms.get("ambient").asVec4f().w, 0.4);
	ASSERT_EQ(*uniforms.get("iv2").asVec2i().i2_, 2);
}

TEST_F(MigrationTest, migrate_from_V12) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V12.rca").string()));

	auto pcam = core::Queries::findByName(racoproject->project()->instances(), "PerspectiveCamera")->as<user_types::PerspectiveCamera>();
	ASSERT_EQ(*pcam->viewport_->offsetX_, 1);
	ASSERT_EQ(*pcam->viewport_->offsetY_, 3);  // linked
	ASSERT_EQ(*pcam->viewport_->width_, 1441);
	ASSERT_EQ(*pcam->viewport_->height_, 721);

	ASSERT_EQ(pcam->frustum_->get("nearPlane")->asDouble(), 4.5);  /// linked
	ASSERT_EQ(pcam->frustum_->get("farPlane")->asDouble(), 1001.0);
	ASSERT_EQ(pcam->frustum_->get("fieldOfView")->asDouble(), 36.0);
	ASSERT_EQ(pcam->frustum_->get("aspectRatio")->asDouble(), 3.0);

	auto ocam = core::Queries::findByName(racoproject->project()->instances(), "OrthographicCamera")->as<user_types::OrthographicCamera>();
	ASSERT_EQ(*ocam->viewport_->offsetX_, 2);
	ASSERT_EQ(*ocam->viewport_->offsetY_, 3);  // linked
	ASSERT_EQ(*ocam->viewport_->width_, 1442);
	ASSERT_EQ(*ocam->viewport_->height_, 722);

	ASSERT_EQ(*ocam->frustum_->near_, 2.1);
	ASSERT_EQ(*ocam->frustum_->far_, 1002.0);
	ASSERT_EQ(*ocam->frustum_->left_, 4.5);	 // linked
	ASSERT_EQ(*ocam->frustum_->right_, 12.0);
	ASSERT_EQ(*ocam->frustum_->bottom_, -8.0);
	ASSERT_EQ(*ocam->frustum_->top_, 12.0);

	auto material = core::Queries::findByName(racoproject->project()->instances(), "Material")->as<user_types::Material>();
	ASSERT_EQ(*material->options_->blendOperationColor_, static_cast<int>(user_types::EBlendOperation::Max));
	ASSERT_EQ(*material->options_->blendOperationAlpha_, static_cast<int>(user_types::EBlendOperation::Add));

	ASSERT_EQ(*material->options_->blendFactorSrcColor_, static_cast<int>(user_types::EBlendFactor::One));
	ASSERT_EQ(*material->options_->blendFactorDestColor_, static_cast<int>(user_types::EBlendFactor::AlphaSaturate));
	ASSERT_EQ(*material->options_->blendFactorSrcAlpha_, static_cast<int>(user_types::EBlendFactor::Zero));
	ASSERT_EQ(*material->options_->blendFactorDestAlpha_, static_cast<int>(user_types::EBlendFactor::AlphaSaturate));

	ASSERT_EQ(*material->options_->depthwrite_, false);
	ASSERT_EQ(*material->options_->depthFunction_, static_cast<int>(user_types::EDepthFunc::Never));
	ASSERT_EQ(*material->options_->cullmode_, static_cast<int>(user_types::ECullMode::FrontAndBackFacing));

	ASSERT_EQ(*material->options_->blendColor_->x, 1.0);
	ASSERT_EQ(*material->options_->blendColor_->y, 2.0);
	ASSERT_EQ(*material->options_->blendColor_->z, 3.0);
	ASSERT_EQ(*material->options_->blendColor_->w, 4.0);

	auto meshnode = core::Queries::findByName(racoproject->project()->instances(), "MeshNode")->as<user_types::MeshNode>();
	auto options = dynamic_cast<const user_types::BlendOptions*>(&meshnode->materials_->get(0)->asTable().get("options")->asStruct());

	ASSERT_EQ(*options->blendOperationColor_, static_cast<int>(user_types::EBlendOperation::Add));
	ASSERT_EQ(*options->blendOperationAlpha_, static_cast<int>(user_types::EBlendOperation::Max));

	ASSERT_EQ(*options->blendFactorSrcColor_, static_cast<int>(user_types::EBlendFactor::AlphaSaturate));
	ASSERT_EQ(*options->blendFactorDestColor_, static_cast<int>(user_types::EBlendFactor::One));
	ASSERT_EQ(*options->blendFactorSrcAlpha_, static_cast<int>(user_types::EBlendFactor::AlphaSaturate));
	ASSERT_EQ(*options->blendFactorDestAlpha_, static_cast<int>(user_types::EBlendFactor::Zero));

	ASSERT_EQ(*options->depthwrite_, false);
	ASSERT_EQ(*options->depthFunction_, static_cast<int>(user_types::EDepthFunc::Never));
	ASSERT_EQ(*options->cullmode_, static_cast<int>(user_types::ECullMode::FrontAndBackFacing));

	ASSERT_EQ(*options->blendColor_->x, 4.0);
	ASSERT_EQ(*options->blendColor_->y, 3.0);
	ASSERT_EQ(*options->blendColor_->z, 2.0);
	ASSERT_EQ(*options->blendColor_->w, 1.0);

	auto meshnode_no_mesh = core::Queries::findByName(racoproject->project()->instances(), "meshnode_no_mesh")->as<user_types::MeshNode>();
	ASSERT_EQ(meshnode_no_mesh->materials_->size(), 0);

	auto meshnode_mesh_no_mat = core::Queries::findByName(racoproject->project()->instances(), "meshnode_mesh_no_mat")->as<user_types::MeshNode>();
	ASSERT_EQ(meshnode_mesh_no_mat->materials_->size(), 1);

	auto lua = core::Queries::findByName(racoproject->project()->instances(), "LuaScript")->as<user_types::LuaScript>();
	checkLinks(*racoproject->project(), {{{lua, {"outputs", "int"}}, {pcam, {"viewport", "offsetY"}}},
											{{lua, {"outputs", "float"}}, {pcam, {"frustum", "nearPlane"}}},
											{{lua, {"outputs", "int"}}, {ocam, {"viewport", "offsetY"}}},
											{{lua, {"outputs", "float"}}, {ocam, {"frustum", "leftPlane"}}}});
}

TEST_F(MigrationTest, migrate_from_V13) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V13.rca").string()));

	auto textureNotFlipped = core::Queries::findByName(racoproject->project()->instances(), "DuckTextureNotFlipped")->as<user_types::Texture>();
	ASSERT_FALSE(*textureNotFlipped->flipTexture_);

	auto textureFlipped = core::Queries::findByName(racoproject->project()->instances(), "DuckTextureFlipped")->as<user_types::Texture>();
	ASSERT_TRUE(*textureFlipped->flipTexture_);
}

TEST_F(MigrationTest, migrate_from_V14) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V14.rca").string()));

	auto camera = core::Queries::findByName(racoproject->project()->instances(), "PerspectiveCamera")->as<user_types::PerspectiveCamera>();
	auto renderpass = core::Queries::findByName(racoproject->project()->instances(), "MainRenderPass")->as<user_types::RenderPass>();
	ASSERT_EQ(*renderpass->camera_, camera);

	auto texture = core::Queries::findByName(racoproject->project()->instances(), "Texture")->as<user_types::Texture>();
	auto mat_no_tex = core::Queries::findByName(racoproject->project()->instances(), "mat_no_tex")->as<user_types::Material>();
	auto mat_with_tex = core::Queries::findByName(racoproject->project()->instances(), "mat_with_tex")->as<user_types::Material>();

	ASSERT_EQ(mat_no_tex->uniforms_->get("u_Tex")->asRef(), nullptr);
	ASSERT_EQ(mat_with_tex->uniforms_->get("u_Tex")->asRef(), texture);

	auto buffer = create<user_types::RenderBuffer>("buffer");
	ASSERT_TRUE(mat_no_tex->uniforms_->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(mat_with_tex->uniforms_->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(mat_no_tex->uniforms_->get("u_Tex")->canSetRef(buffer));
	ASSERT_TRUE(mat_with_tex->uniforms_->get("u_Tex")->canSetRef(buffer));

	auto meshnode_no_tex = core::Queries::findByName(racoproject->project()->instances(), "meshnode_no_tex")->as<user_types::MeshNode>();
	auto meshnode_with_tex = core::Queries::findByName(racoproject->project()->instances(), "meshnode_with_tex")->as<user_types::MeshNode>();

	ASSERT_EQ(meshnode_no_tex->getUniformContainer(0)->get("u_Tex")->asRef(), nullptr);
	ASSERT_EQ(meshnode_with_tex->getUniformContainer(0)->get("u_Tex")->asRef(), texture);

	ASSERT_TRUE(meshnode_no_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(meshnode_with_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(meshnode_no_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(buffer));
	ASSERT_TRUE(meshnode_with_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(buffer));
}

TEST_F(MigrationTest, migrate_from_V14b) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V14b.rca").string()));

	auto camera = core::Queries::findByName(racoproject->project()->instances(), "OrthographicCamera")->as<user_types::OrthographicCamera>();
	auto renderpass = core::Queries::findByName(racoproject->project()->instances(), "MainRenderPass")->as<user_types::RenderPass>();
	ASSERT_EQ(*renderpass->camera_, camera);

	auto texture = core::Queries::findByName(racoproject->project()->instances(), "Texture")->as<user_types::Texture>();
	auto mat_no_tex = core::Queries::findByName(racoproject->project()->instances(), "mat_no_tex")->as<user_types::Material>();
	auto mat_with_tex = core::Queries::findByName(racoproject->project()->instances(), "mat_with_tex")->as<user_types::Material>();

	ASSERT_EQ(mat_no_tex->uniforms_->get("u_Tex")->asRef(), nullptr);
	ASSERT_EQ(mat_with_tex->uniforms_->get("u_Tex")->asRef(), texture);

	auto buffer = create<user_types::RenderBuffer>("buffer");
	ASSERT_TRUE(mat_no_tex->uniforms_->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(mat_with_tex->uniforms_->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(mat_no_tex->uniforms_->get("u_Tex")->canSetRef(buffer));
	ASSERT_TRUE(mat_with_tex->uniforms_->get("u_Tex")->canSetRef(buffer));

	auto meshnode_no_tex = core::Queries::findByName(racoproject->project()->instances(), "meshnode_no_tex")->as<user_types::MeshNode>();
	auto meshnode_with_tex = core::Queries::findByName(racoproject->project()->instances(), "meshnode_with_tex")->as<user_types::MeshNode>();

	ASSERT_EQ(meshnode_no_tex->getUniformContainer(0)->get("u_Tex")->asRef(), nullptr);
	ASSERT_EQ(meshnode_with_tex->getUniformContainer(0)->get("u_Tex")->asRef(), texture);

	ASSERT_TRUE(meshnode_no_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(meshnode_with_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(meshnode_no_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(buffer));
	ASSERT_TRUE(meshnode_with_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(buffer));
}

TEST_F(MigrationTest, migrate_from_V14c) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V14c.rca").string()));

	auto renderpass = core::Queries::findByName(racoproject->project()->instances(), "MainRenderPass")->as<user_types::RenderPass>();
	ASSERT_EQ(*renderpass->camera_, nullptr);

	auto texture = core::Queries::findByName(racoproject->project()->instances(), "Texture")->as<user_types::Texture>();
	auto mat_no_tex = core::Queries::findByName(racoproject->project()->instances(), "mat_no_tex")->as<user_types::Material>();
	auto mat_with_tex = core::Queries::findByName(racoproject->project()->instances(), "mat_with_tex")->as<user_types::Material>();

	ASSERT_EQ(mat_no_tex->uniforms_->get("u_Tex")->asRef(), nullptr);
	ASSERT_EQ(mat_with_tex->uniforms_->get("u_Tex")->asRef(), texture);

	auto buffer = create<user_types::RenderBuffer>("buffer");
	ASSERT_TRUE(mat_no_tex->uniforms_->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(mat_with_tex->uniforms_->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(mat_no_tex->uniforms_->get("u_Tex")->canSetRef(buffer));
	ASSERT_TRUE(mat_with_tex->uniforms_->get("u_Tex")->canSetRef(buffer));

	auto meshnode_no_tex = core::Queries::findByName(racoproject->project()->instances(), "meshnode_no_tex")->as<user_types::MeshNode>();
	auto meshnode_with_tex = core::Queries::findByName(racoproject->project()->instances(), "meshnode_with_tex")->as<user_types::MeshNode>();

	ASSERT_EQ(meshnode_no_tex->getUniformContainer(0)->get("u_Tex")->asRef(), nullptr);
	ASSERT_EQ(meshnode_with_tex->getUniformContainer(0)->get("u_Tex")->asRef(), texture);

	ASSERT_TRUE(meshnode_no_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(meshnode_with_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(texture));
	ASSERT_TRUE(meshnode_no_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(buffer));
	ASSERT_TRUE(meshnode_with_tex->getUniformContainer(0)->get("u_Tex")->canSetRef(buffer));
}

TEST_F(MigrationTest, migrate_from_V16) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V16.rca").string()));

	auto renderlayeropt = core::Queries::findByName(racoproject->project()->instances(), "RenderLayerOptimized")->as<user_types::RenderLayer>();
	ASSERT_EQ(renderlayeropt->sortOrder_.asInt(), static_cast<int>(user_types::ERenderLayerOrder::Manual));
	auto renderlayermanual = core::Queries::findByName(racoproject->project()->instances(), "RenderLayerManual")->as<user_types::RenderLayer>();
	ASSERT_EQ(renderlayermanual->sortOrder_.asInt(), static_cast<int>(user_types::ERenderLayerOrder::Manual));
	auto renderlayerscenegraph = core::Queries::findByName(racoproject->project()->instances(), "RenderLayerSceneGraph")->as<user_types::RenderLayer>();
	ASSERT_EQ(renderlayerscenegraph->sortOrder_.asInt(), static_cast<int>(user_types::ERenderLayerOrder::SceneGraph));
}

TEST_F(MigrationTest, migrate_from_V18) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V18.rca").string()));

	auto bgColor = racoproject->project()->settings()->backgroundColor_;
	ASSERT_EQ(bgColor->typeDescription.typeName, Vec4f::typeDescription.typeName);

	ASSERT_EQ(bgColor->x.asDouble(), 0.3);
	ASSERT_EQ(bgColor->y.asDouble(), 0.2);
	ASSERT_EQ(bgColor->z.asDouble(), 0.1);
	ASSERT_EQ(bgColor->w.asDouble(), 1.0);
}

TEST_F(MigrationTest, migrate_from_V21) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V21.rca").string()));

	auto resourceFolders = racoproject->project()->settings()->defaultResourceDirectories_;
	ASSERT_EQ(resourceFolders->typeDescription.typeName, DefaultResourceDirectories::typeDescription.typeName);

	ASSERT_EQ(resourceFolders->imageSubdirectory_.asString(), "images");
	ASSERT_EQ(resourceFolders->meshSubdirectory_.asString(), "meshes");
	ASSERT_EQ(resourceFolders->scriptSubdirectory_.asString(), "scripts");
	ASSERT_EQ(resourceFolders->shaderSubdirectory_.asString(), "shaders");
}

TEST_F(MigrationTest, migrate_from_V21_custom_paths) {
	std::string imageSubdirectory = "imgs";
	std::string meshSubdirectory = "mshs";
	std::string scriptSubdirectory = "spts";
	std::string shaderSubdirectory = "shds";

	auto preferencesFile = core::PathManager::preferenceFilePath();
	if (preferencesFile.exists()) {
		std::filesystem::remove(preferencesFile);
	}

	{
		// use scope to force saving QSettings when leaving the scope
		QSettings settings(preferencesFile.string().c_str(), QSettings::IniFormat);
		settings.setValue("imageSubdirectory", imageSubdirectory.c_str());
		settings.setValue("meshSubdirectory", meshSubdirectory.c_str());
		settings.setValue("scriptSubdirectory", scriptSubdirectory.c_str());
		settings.setValue("shaderSubdirectory", shaderSubdirectory.c_str());
	}

	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V21.rca").string()));

	auto resourceFolders = racoproject->project()->settings()->defaultResourceDirectories_;
	ASSERT_EQ(resourceFolders->typeDescription.typeName, DefaultResourceDirectories::typeDescription.typeName);

	ASSERT_EQ(resourceFolders->imageSubdirectory_.asString(), imageSubdirectory);
	ASSERT_EQ(resourceFolders->meshSubdirectory_.asString(), meshSubdirectory);
	ASSERT_EQ(resourceFolders->scriptSubdirectory_.asString(), scriptSubdirectory);
	ASSERT_EQ(resourceFolders->shaderSubdirectory_.asString(), shaderSubdirectory);

	if (preferencesFile.exists()) {
		std::filesystem::remove(preferencesFile);
	}
}

TEST_F(MigrationTest, migrate_from_V23) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V23.rca").string()));

	auto prefab = core::Queries::findByName(racoproject->project()->instances(), "Prefab")->as<user_types::Prefab>();
	auto inst = core::Queries::findByName(racoproject->project()->instances(), "PrefabInstance")->as<user_types::PrefabInstance>();
	auto prefab_node = prefab->children_->asVector<SEditorObject>()[0]->as<user_types::Node>();
	auto inst_node = inst->children_->asVector<SEditorObject>()[0]->as<user_types::Node>();

	EXPECT_EQ(inst_node->objectID(), EditorObject::XorObjectIDs(prefab_node->objectID(), inst->objectID()));
}

TEST_F(MigrationTest, migrate_from_V29) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V29.rca").string()));

	auto animation = core::Queries::findByName(racoproject->project()->instances(), "Animation")->as<user_types::Animation>();
	auto lua = core::Queries::findByName(racoproject->project()->instances(), "LuaScript")->as<user_types::LuaScript>();

	EXPECT_TRUE(lua->outputs_->hasProperty("flag"));
	EXPECT_EQ(racoproject->project()->links().size(), 0);

	EXPECT_FALSE(animation->hasProperty("play"));
	EXPECT_FALSE(animation->hasProperty("loop"));
	EXPECT_FALSE(animation->hasProperty("rewindOnStop"));
}

TEST_F(MigrationTest, migrate_V29_to_V33) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V29_tags.rca").string()));

	auto node = core::Queries::findByName(racoproject->project()->instances(), "Node")->as<user_types::Node>();
	auto mat_front = core::Queries::findByName(racoproject->project()->instances(), "mat_front")->as<user_types::Material>();
	auto mat_back = core::Queries::findByName(racoproject->project()->instances(), "mat_back")->as<user_types::Material>();

	auto renderlayermanual = core::Queries::findByName(racoproject->project()->instances(), "RenderLayerManual")->as<user_types::RenderLayer>();

	EXPECT_EQ(node->tags_->asVector<std::string>(), std::vector<std::string>({"render_main"}));

	EXPECT_EQ(mat_front->tags_->asVector<std::string>(), std::vector<std::string>({"mat_front"}));
	EXPECT_EQ(mat_back->tags_->asVector<std::string>(), std::vector<std::string>({"mat_back"}));

	EXPECT_EQ(renderlayermanual->tags_->asVector<std::string>(), std::vector<std::string>({"debug"}));
	EXPECT_EQ(renderlayermanual->materialFilterTags_->asVector<std::string>(), std::vector<std::string>({"mat_front", "mat_back"}));

	EXPECT_EQ(renderlayermanual->renderableTags_->size(), 1);
	EXPECT_TRUE(renderlayermanual->renderableTags_->get("render_main") != nullptr);
	EXPECT_EQ(renderlayermanual->renderableTags_->get("render_main")->asInt(), 2);
}

TEST_F(MigrationTest, migrate_V30_to_V34) {
	// We would like a V30 file but we can't produce that after merging V28/V29 from master anymore.
	// So we use a V29 file instead.
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V29_renderlayer.rca").string()));

	auto layer_excl = core::Queries::findByName(racoproject->project()->instances(), "layer_exclusive")->as<user_types::RenderLayer>();
	auto layer_incl = core::Queries::findByName(racoproject->project()->instances(), "layer_inclusive")->as<user_types::RenderLayer>();

	EXPECT_EQ(*layer_excl->materialFilterMode_, static_cast<int>(user_types::ERenderLayerMaterialFilterMode::Exclusive));
	EXPECT_EQ(*layer_incl->materialFilterMode_, static_cast<int>(user_types::ERenderLayerMaterialFilterMode::Inclusive));
}

TEST_F(MigrationTest, migrate_from_V35) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V35.rca").string()));

	auto prefab = core::Queries::findByName(racoproject->project()->instances(), "Prefab")->as<user_types::Prefab>();
	auto inst = core::Queries::findByName(racoproject->project()->instances(), "PrefabInstance")->as<user_types::PrefabInstance>();
	auto global_lua = core::Queries::findByName(racoproject->project()->instances(), "global_control")->as<user_types::LuaScript>();

	auto prefab_lua_types = raco::select<user_types::LuaScript>(prefab->children_->asVector<SEditorObject>(), "types-scalar");
	auto prefab_int_types = raco::select<user_types::LuaInterface>(prefab->children_->asVector<SEditorObject>(), "types-scalar");
	auto prefab_int_array = raco::select<user_types::LuaInterface>(prefab->children_->asVector<SEditorObject>(), "array");

	EXPECT_EQ(prefab_int_types->inputs_->get("float")->asDouble(), 1.0);
	EXPECT_EQ(prefab_int_types->inputs_->get("integer")->asInt(), 2);
	EXPECT_EQ(prefab_int_types->inputs_->get("integer64")->asInt64(), 3);

	auto inst_lua_types = raco::select<user_types::LuaScript>(inst->children_->asVector<SEditorObject>(), "types-scalar");
	auto inst_int_types = raco::select<user_types::LuaInterface>(inst->children_->asVector<SEditorObject>(), "types-scalar");
	auto inst_int_array = raco::select<user_types::LuaInterface>(inst->children_->asVector<SEditorObject>(), "array");

	EXPECT_EQ(inst_int_types->objectID(), EditorObject::XorObjectIDs(prefab_int_types->objectID(), inst->objectID()));

	EXPECT_EQ(inst_int_types->inputs_->get("float")->asDouble(), 0.25);
	EXPECT_EQ(inst_int_types->inputs_->get("integer")->asInt(), 8);
	EXPECT_EQ(inst_int_types->inputs_->get("integer64")->asInt64(), 9);

	for (const auto& link : racoproject->project()->links()) {
		EXPECT_TRUE(Queries::linkWouldBeAllowed(*racoproject->project(), link->startProp(), link->endProp(), false));
	}

	EXPECT_EQ(nullptr, Queries::getLink(*racoproject->project(), {prefab_int_array, {"inputs", "float_array", "1"}}));

	auto inst_link = Queries::getLink(*racoproject->project(), {inst_int_array, {"inputs", "float_array", "1"}});
	EXPECT_TRUE(inst_link != nullptr);
	EXPECT_EQ(inst_link->startProp(), PropertyDescriptor(inst_lua_types, {"outputs", "bar"}));
}

TEST_F(MigrationTest, migrate_from_V35_extref) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V35_extref.rca").string()));

	auto prefab = core::Queries::findByName(racoproject->project()->instances(), "Prefab")->as<user_types::Prefab>();
	auto inst = core::Queries::findByName(racoproject->project()->instances(), "PrefabInstance")->as<user_types::PrefabInstance>();
	auto global_lua = core::Queries::findByName(racoproject->project()->instances(), "global_control")->as<user_types::LuaScript>();

	auto prefab_lua_types = raco::select<user_types::LuaScript>(prefab->children_->asVector<SEditorObject>(), "types-scalar");
	auto prefab_int_types = raco::select<user_types::LuaInterface>(prefab->children_->asVector<SEditorObject>(), "types-scalar");

	EXPECT_EQ(prefab_int_types->inputs_->get("float")->asDouble(), 1.0);
	EXPECT_EQ(prefab_int_types->inputs_->get("integer")->asInt(), 2);
	EXPECT_EQ(prefab_int_types->inputs_->get("integer64")->asInt64(), 3);

	auto inst_lua_types = raco::select<user_types::LuaScript>(inst->children_->asVector<SEditorObject>(), "types-scalar");
	auto inst_int_types = raco::select<user_types::LuaInterface>(inst->children_->asVector<SEditorObject>(), "types-scalar");

	EXPECT_EQ(inst_int_types->objectID(), EditorObject::XorObjectIDs(prefab_int_types->objectID(), inst->objectID()));

	EXPECT_EQ(inst_int_types->inputs_->get("float")->asDouble(), 0.25);
	EXPECT_EQ(inst_int_types->inputs_->get("integer")->asInt(), 8);
	EXPECT_EQ(inst_int_types->inputs_->get("integer64")->asInt64(), 9);

	for (const auto& link : racoproject->project()->links()) {
		EXPECT_TRUE(Queries::linkWouldBeAllowed(*racoproject->project(), link->startProp(), link->endProp(), false));
	}
}

TEST_F(MigrationTest, migrate_from_V35_extref_nested) {
	application.switchActiveRaCoProject(QString::fromStdString((test_path() / "migrationTestData" / "V35_extref_nested.rca").string()), {});
	auto racoproject = &application.activeRaCoProject();

	auto prefab = core::Queries::findByName(racoproject->project()->instances(), "Prefab")->as<user_types::Prefab>();
	auto inst = core::Queries::findByName(racoproject->project()->instances(), "PrefabInstance")->as<user_types::PrefabInstance>();
	auto global_lua = core::Queries::findByName(racoproject->project()->instances(), "global_control")->as<user_types::LuaScript>();

	auto prefab_lua_types = raco::select<user_types::LuaScript>(prefab->children_->asVector<SEditorObject>(), "types-scalar");
	auto prefab_int_types = raco::select<user_types::LuaInterface>(prefab->children_->asVector<SEditorObject>(), "types-scalar");

	EXPECT_EQ(prefab_int_types->inputs_->get("float")->asDouble(), 1.0);
	EXPECT_EQ(prefab_int_types->inputs_->get("integer")->asInt(), 2);
	EXPECT_EQ(prefab_int_types->inputs_->get("integer64")->asInt64(), 3);

	auto inst_nested = Queries::findByName(inst->children_->asVector<SEditorObject>(), "inst_nested");

	auto inst_lua_types = raco::select<user_types::LuaScript>(inst_nested->children_->asVector<SEditorObject>(), "types-scalar");
	auto inst_int_types = raco::select<user_types::LuaInterface>(inst_nested->children_->asVector<SEditorObject>(), "types-scalar");
	auto inst_int_array = raco::select<user_types::LuaInterface>(inst_nested->children_->asVector<SEditorObject>(), "array");

	EXPECT_EQ(inst_int_types->objectID(), EditorObject::XorObjectIDs(prefab_int_types->objectID(), inst_nested->objectID()));

	EXPECT_EQ(inst_int_types->inputs_->get("float")->asDouble(), 0.25);

	for (const auto& link : racoproject->project()->links()) {
		EXPECT_TRUE(Queries::linkWouldBeAllowed(*racoproject->project(), link->startProp(), link->endProp(), false));
	}

	auto inst_link = Queries::getLink(*racoproject->project(), {inst_int_array, {"inputs", "float_array", "1"}});
	EXPECT_TRUE(inst_link != nullptr);
	EXPECT_EQ(inst_link->startProp(), PropertyDescriptor(inst_lua_types, {"outputs", "bar"}));
}

TEST_F(MigrationTest, migrate_from_V39) {
	application.switchActiveRaCoProject(QString::fromStdString((test_path() / "migrationTestData" / "V39.rca").string()), {});
	auto racoproject = &application.activeRaCoProject();
	auto instances = racoproject->project()->instances();
	const auto DELTA = 0.001;

	auto luascript = core::Queries::findByName(instances, "LuaScript");
	ASSERT_NE(ValueHandle(luascript, {"inputs", "integer64"}).asInt64(), int64_t{0});

	auto luascript1 = core::Queries::findByName(instances, "LuaScript (1)");
	ASSERT_NEAR(ValueHandle(luascript1, {"inputs", "vector3f", "x"}).asDouble(), 0.54897, DELTA);
	ASSERT_NEAR(ValueHandle(luascript1, {"inputs", "vector3f", "y"}).asDouble(), 1.09794, DELTA);
	ASSERT_NEAR(ValueHandle(luascript1, {"inputs", "vector3f", "z"}).asDouble(), 3.0, DELTA);

	auto luainterface = core::Queries::findByName(instances, "LuaInterface");
	ASSERT_EQ(ValueHandle(luainterface, {"inputs", "integer"}).asInt(), 2);

	auto luainterface1 = core::Queries::findByName(instances, "LuaInterface (1)");
	ASSERT_EQ(ValueHandle(luainterface1, {"inputs", "integer"}).asInt(), 2);

	auto luascript2 = core::Queries::findByName(instances, "LuaScript (2)");
	ASSERT_NEAR(ValueHandle(luascript2, {"outputs", "ovector3f", "x"}).asDouble(), 0.53866, DELTA);
	ASSERT_NEAR(ValueHandle(luascript2, {"outputs", "ovector3f", "y"}).asDouble(), 1.07732, DELTA);
	ASSERT_NEAR(ValueHandle(luascript2, {"outputs", "ovector3f", "z"}).asDouble(), 3.0, DELTA);

	auto node = core::Queries::findByName(instances, "Node");
	ASSERT_TRUE(ValueHandle(node, {"visibility"}).asBool());
	ASSERT_NEAR(ValueHandle(node, {"scaling", "x"}).asDouble(), 0.54897, DELTA);
	ASSERT_NEAR(ValueHandle(node, {"scaling", "y"}).asDouble(), 1.09794, DELTA);
	ASSERT_NEAR(ValueHandle(node, {"scaling", "z"}).asDouble(), 3.0, DELTA);

	auto meshNode = core::Queries::findByName(instances, "MeshNode");
	ASSERT_TRUE(ValueHandle(meshNode, {"visibility"}).asBool());
	ASSERT_NEAR(ValueHandle(meshNode, {"scaling", "x"}).asDouble(), 0.54897, DELTA);
	ASSERT_NEAR(ValueHandle(meshNode, {"scaling", "y"}).asDouble(), 1.09794, DELTA);
	ASSERT_NEAR(ValueHandle(meshNode, {"scaling", "z"}).asDouble(), 3.0, DELTA);

	auto timer = core::Queries::findByName(instances, "Timer");
	ASSERT_EQ(ValueHandle(timer, {"inputs", "ticker_us"}).asInt64(), int64_t{0});
	ASSERT_NE(ValueHandle(timer, {"outputs", "ticker_us"}).asInt64(), int64_t{0});

	ASSERT_EQ(racoproject->project()->links().size(), 12);
	for (const auto& link : racoproject->project()->links()) {
		ASSERT_TRUE(link->isValid());
	}
}

TEST_F(MigrationTest, migrate_from_V40) {
	application.switchActiveRaCoProject(QString::fromStdString((test_path() / "migrationTestData" / "V40.rca").string()), {});
	auto racoproject = &application.activeRaCoProject();
	auto instances = racoproject->project()->instances();

	auto node = core::Queries::findByName(instances, "Node");
	auto animation = core::Queries::findByName(instances, "Animation");

	checkLinks(*racoproject->project(), {{{animation, {"outputs", "Ch0.AnimationChannel"}}, {node, {"translation"}}, true}});
}

TEST_F(MigrationTest, migrate_from_V41) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V41.rca").string()));

	auto start = core::Queries::findByName(racoproject->project()->instances(), "start")->as<user_types::LuaScript>();
	auto end = core::Queries::findByName(racoproject->project()->instances(), "end")->as<user_types::LuaScript>();

	checkLinks(*racoproject->project(), {{{start, {"outputs", "ofloat"}}, {end, {"inputs", "float"}}, true, false}});
}

TEST_F(MigrationTest, migrate_to_V43) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V41.rca").string()));

	auto settings = core::Queries::findByName(racoproject->project()->instances(), "V41")->as<core::ProjectSettings>();

	EXPECT_FALSE(settings->hasProperty("enableTimerFlag"));
	EXPECT_FALSE(settings->hasProperty("runTimer"));
}

TEST_F(MigrationTest, migrate_from_V43) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V43.rca").string()));

	auto pcam = core::Queries::findByName(racoproject->project()->instances(), "PerspectiveCamera")->as<user_types::PerspectiveCamera>();

	ASSERT_EQ(pcam->frustum_->get("nearPlane")->asDouble(), 0.2);
	ASSERT_EQ(pcam->frustum_->get("farPlane")->asDouble(), 42.0);  // linked
	ASSERT_EQ(pcam->frustum_->get("fieldOfView")->asDouble(), 4.0);
	ASSERT_EQ(pcam->frustum_->get("aspectRatio")->asDouble(), 5.0);

	auto renderpass = core::Queries::findByName(racoproject->project()->instances(), "RenderPass")->as<user_types::RenderPass>();

	EXPECT_EQ(*renderpass->enabled_, false);
	EXPECT_EQ(*renderpass->renderOrder_, 7);
	EXPECT_EQ(*renderpass->clearColor_->x, 1.0);
	EXPECT_EQ(*renderpass->clearColor_->y, 2.0);
	EXPECT_EQ(*renderpass->clearColor_->z, 3.0);
	EXPECT_EQ(*renderpass->clearColor_->w, 4.0);

	auto lua = core::Queries::findByName(racoproject->project()->instances(), "LuaScript")->as<user_types::LuaScript>();
	checkLinks(*racoproject->project(), {{{{lua, {"outputs", "float"}}, {pcam, {"frustum", "farPlane"}}}}});
}

TEST_F(MigrationTest, migrate_from_V44) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V44.rca").string()));

	auto layer = core::Queries::findByName(racoproject->project()->instances(), "MainRenderLayer")->as<user_types::RenderLayer>();

	auto anno_red = layer->renderableTags_->get("red")->query<LinkEndAnnotation>();
	EXPECT_TRUE(anno_red != nullptr);
	// migration to V2001 reset feature level to 1
	EXPECT_EQ(*anno_red->featureLevel_, 1);
	auto anno_green = layer->renderableTags_->get("green")->query<LinkEndAnnotation>();
	EXPECT_TRUE(anno_green != nullptr);
	// migration to V2001 reset feature level to 1
	EXPECT_EQ(*anno_green->featureLevel_, 1);
}

TEST_F(MigrationTest, migrate_from_V45) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V45.rca").string()));

	auto perspCamera = core::Queries::findByName(racoproject->project()->instances(), "PerspectiveCamera")->as<user_types::PerspectiveCamera>();

	EXPECT_EQ(*perspCamera->viewport_->get("width")->query<RangeAnnotation<int>>()->min_, 1);
	EXPECT_EQ(*perspCamera->viewport_->get("height")->query<RangeAnnotation<int>>()->min_, 1);

	auto orthoCamera = core::Queries::findByName(racoproject->project()->instances(), "OrthographicCamera")->as<user_types::OrthographicCamera>();

	EXPECT_EQ(*orthoCamera->viewport_->get("width")->query<RangeAnnotation<int>>()->min_, 1);
	EXPECT_EQ(*orthoCamera->viewport_->get("height")->query<RangeAnnotation<int>>()->min_, 1);

	auto renderTarget = core::Queries::findByName(racoproject->project()->instances(), "RenderTarget")->as<user_types::RenderTarget>();

	EXPECT_TRUE(renderTarget->buffers_.query<ExpectEmptyReference>() != nullptr);
}

TEST_F(MigrationTest, migrate_from_V50) {
	using namespace raco;

	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V50.rca").string()));

	auto intf_scalar = core::Queries::findByName(racoproject->project()->instances(), "intf-scalar")->as<user_types::LuaInterface>();
	auto intf_array = core::Queries::findByName(racoproject->project()->instances(), "intf-array")->as<user_types::LuaInterface>();
	auto intf_struct = core::Queries::findByName(racoproject->project()->instances(), "intf-struct")->as<user_types::LuaInterface>();

	auto texture = core::Queries::findByName(racoproject->project()->instances(), "texture");

	auto node = core::Queries::findByName(racoproject->project()->instances(), "Node");
	auto meshnode_no_mat = core::Queries::findByName(racoproject->project()->instances(), "meshnode_no_mat");

	auto mat_scalar = core::Queries::findByName(racoproject->project()->instances(), "mat_scalar")->as<user_types::Material>();
	EXPECT_EQ(ValueHandle(mat_scalar, {"uniforms", "i"}).asInt(), 2);
	checkVec2iValue(ValueHandle(mat_scalar, {"uniforms", "iv2"}), {1, 2});

	auto mat_array_link_array = core::Queries::findByName(racoproject->project()->instances(), "mat_array_link_array")->as<user_types::Material>();
	EXPECT_EQ(ValueHandle(mat_array_link_array, {"uniforms", "ivec", "1"}).asInt(), 1);
	EXPECT_EQ(ValueHandle(mat_array_link_array, {"uniforms", "ivec", "2"}).asInt(), 2);

	auto mat_array_link_member = core::Queries::findByName(racoproject->project()->instances(), "mat_array_link_member")->as<user_types::Material>();

	auto mat_struct = core::Queries::findByName(racoproject->project()->instances(), "mat_struct")->as<user_types::Material>();
	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_prims", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "s_prims", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_samplers", "s_texture"}).asRef(), texture);

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "nested", "prims", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "nested", "prims", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "a_s_prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "a_s_prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_a_struct_prim", "prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "s_a_struct_prim", "prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "a_s_a_struct_prim", "1", "prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "a_s_a_struct_prim", "1", "prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_a_prims", "ivec", "1"}).asInt(), 1);
	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_a_prims", "ivec", "2"}).asInt(), 2);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "s_a_prims", "aivec2", "1"}), {1, 2});
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "s_a_prims", "aivec2", "2"}), {3, 4});


	auto meshnode_mat_scalar = core::Queries::findByName(racoproject->project()->instances(), "meshnode_mat_scalar");
	EXPECT_EQ(ValueHandle(meshnode_mat_scalar, {"materials", "material", "uniforms", "i"}).asInt(), 2);
	checkVec2iValue(ValueHandle(meshnode_mat_scalar, {"materials", "material", "uniforms", "iv2"}), {1, 2});

	auto meshnode_mat_array_link_array = core::Queries::findByName(racoproject->project()->instances(), "meshnode_mat_array_link_array");
	EXPECT_EQ(ValueHandle(meshnode_mat_array_link_array, {"materials", "material", "uniforms", "ivec", "1"}).asInt(), 1);
	EXPECT_EQ(ValueHandle(meshnode_mat_array_link_array, {"materials", "material", "uniforms", "ivec", "2"}).asInt(), 2);

	auto meshnode_mat_array_link_member = core::Queries::findByName(racoproject->project()->instances(), "meshnode_mat_array_link_member");

	auto meshnode_mat_struct = core::Queries::findByName(racoproject->project()->instances(), "meshnode_mat_struct");
	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_prims", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_prims", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_samplers", "s_texture"}).asRef(), texture);
	
	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "nested", "prims", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "nested", "prims", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_struct_prim", "prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_struct_prim", "prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_a_struct_prim", "1", "prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_a_struct_prim", "1", "prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "ivec", "1"}).asInt(), 1);
	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "ivec", "2"}).asInt(), 2);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "aivec2", "1"}), {1, 2});
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "aivec2", "2"}), {3, 4});

	checkLinks(*racoproject->project(),
		{
			{{intf_scalar, {"inputs", "bool"}}, {node, {"visibility"}}, true, false},
			{{intf_scalar, {"inputs", "bool"}}, {meshnode_no_mat, {"visibility"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_scalar, {"uniforms", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector2f"}}, {mat_scalar, {"uniforms", "v2"}}, true, false},
			{{intf_array, {"inputs", "fvec"}}, {mat_array_link_array, {"uniforms", "fvec"}}, true, false},
			{{intf_scalar, {"inputs", "float"}}, {mat_array_link_member, {"uniforms", "fvec", "5"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_array_link_member, {"uniforms", "avec3", "2"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "s_prims", "f"}}, true, false},

			{{intf_array, {"inputs", "fvec"}}, {mat_struct, {"uniforms", "s_a_prims", "fvec"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "s_a_prims", "avec3", "1"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "nested", "prims", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "nested", "prims", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "s_a_struct_prim", "prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "s_a_struct_prim", "prims", "1", "v3"}}, true, false},
		
			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "a_s_prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "a_s_prims", "1", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "a_s_a_struct_prim", "1", "prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "a_s_a_struct_prim", "1", "prims", "1", "v3"}}, true, false},


			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_scalar, {"materials", "material", "uniforms", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector2f"}}, {meshnode_mat_scalar, {"materials", "material", "uniforms", "v2"}}, true, false},
			{{intf_array, {"inputs", "fvec"}}, {meshnode_mat_array_link_array, {"materials", "material", "uniforms", "fvec"}}, true, false},
			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_array_link_member, {"materials", "material", "uniforms", "fvec", "5"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_array_link_member, {"materials", "material", "uniforms", "avec3", "2"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_prims", "f"}}, true, false},

			{{intf_array, {"inputs", "fvec"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "fvec"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "avec3", "1"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "nested", "prims", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "nested", "prims", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_struct_prim", "prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_struct_prim", "prims", "1", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_prims", "1", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_a_struct_prim", "1", "prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_a_struct_prim", "1", "prims", "1", "v3"}}, true, false}
		
		});
}

TEST_F(MigrationTest, migrate_from_V51_vec_struct_link_validity_update) {
	// This is technically not a migration test, but it seems to be the best way to check
	// that the validity of links which could be created before (but not anymore) is correctly
	// updated during load.
	using namespace raco;

	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V51_link_vec_struct.rca").string()));

	auto start = core::Queries::findByName(racoproject->project()->instances(), "start")->as<user_types::LuaInterface>();
	auto end = core::Queries::findByName(racoproject->project()->instances(), "end")->as<user_types::LuaInterface>();

	checkLinks(*racoproject->project(),
		{{{start, {"inputs", "v3f"}}, {end, {"inputs", "s3f"}}, false, false},
			{{start, {"inputs", "v3i"}}, {end, {"inputs", "s3i"}}, false, false}});
}

TEST_F(MigrationTest, migrate_from_V51) {
	using namespace raco;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V51.rca").string()));

	auto meshnode = core::Queries::findByName(racoproject->project()->instances(), "MeshNode")->as<user_types::MeshNode>();

	EXPECT_EQ(*meshnode->instanceCount_, 4);
	EXPECT_TRUE(core::Queries::isValidLinkEnd(*racoproject->project(), {meshnode, &user_types::MeshNode::instanceCount_}));
}

TEST_F(MigrationTest, migrate_from_V52) {
	using namespace raco;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V52.rca").string()));

	auto cubeMap = core::Queries::findByName(racoproject->project()->instances(), "CubeMap")->as<user_types::CubeMap>();
	EXPECT_EQ(cubeMap->uriFront_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->uriBack_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->uriLeft_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->uriRight_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->uriTop_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->uriBottom_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);

	EXPECT_EQ(cubeMap->level2uriFront_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level2uriBack_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level2uriLeft_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level2uriRight_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level2uriTop_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level2uriBottom_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);

	EXPECT_EQ(cubeMap->level3uriFront_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level3uriBack_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level3uriLeft_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level3uriRight_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level3uriTop_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level3uriBottom_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);

	EXPECT_EQ(cubeMap->level4uriFront_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level4uriBack_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level4uriLeft_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level4uriRight_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level4uriTop_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(cubeMap->level4uriBottom_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);


	auto texture = core::Queries::findByName(racoproject->project()->instances(), "Texture")->as<user_types::Texture>();
	EXPECT_EQ(texture->uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(texture->level2uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(texture->level3uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);
	EXPECT_EQ(texture->level4uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Image);


	auto mesh = core::Queries::findByName(racoproject->project()->instances(), "Mesh")->as<user_types::Mesh>();
	EXPECT_EQ(mesh->uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Mesh);

	auto animationChannel = core::Queries::findByName(racoproject->project()->instances(), "AnimationChannel")->as<user_types::AnimationChannel >();
	EXPECT_EQ(animationChannel ->uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Mesh);

	auto skin = core::Queries::findByName(racoproject->project()->instances(), "Skin")->as<user_types::Skin>();
	EXPECT_EQ(skin->uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Mesh);


	auto luascript = core::Queries::findByName(racoproject->project()->instances(), "LuaScript")->as<user_types::LuaScript>();
	EXPECT_EQ(luascript->uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Script);

	auto luamodule = core::Queries::findByName(racoproject->project()->instances(), "LuaScriptModule")->as<user_types::LuaScriptModule>();
	EXPECT_EQ(luamodule->uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Script);


	auto luainterface = core::Queries::findByName(racoproject->project()->instances(), "LuaInterface")->as<user_types::LuaInterface>();
	EXPECT_EQ(luainterface->uri_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Interface);


	auto material = core::Queries::findByName(racoproject->project()->instances(), "Material")->as<user_types::Material>();
	EXPECT_EQ(material->uriVertex_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Shader);
	EXPECT_EQ(material->uriGeometry_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Shader);
	EXPECT_EQ(material->uriFragment_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Shader);
	EXPECT_EQ(material->uriDefines_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Shader);


	auto settings = core::Queries::findByName(racoproject->project()->instances(), "V52")->as<user_types::ProjectSettings>();
	EXPECT_EQ(settings->defaultResourceDirectories_->imageSubdirectory_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Project);
	EXPECT_EQ(settings->defaultResourceDirectories_->meshSubdirectory_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Project);
	EXPECT_EQ(settings->defaultResourceDirectories_->scriptSubdirectory_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Project);
	EXPECT_EQ(settings->defaultResourceDirectories_->interfaceSubdirectory_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Project);
	EXPECT_EQ(settings->defaultResourceDirectories_->shaderSubdirectory_.query<core::URIAnnotation>()->getFolderTypeKey(), core::PathManager::FolderTypeKeys::Project);
}


TEST_F(MigrationTest, migrate_from_V54) {
	using namespace raco;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V54.rca").string()));

	auto node = core::Queries::findByName(racoproject->project()->instances(), "Node")->as<user_types::Node>();
	auto meshnode = core::Queries::findByName(racoproject->project()->instances(), "MeshNode")->as<user_types::MeshNode>();
	auto nodePrefab = core::Queries::findByName(racoproject->project()->instances(), "NodePrefab")->as<user_types::Node>();
	auto prefab = core::Queries::findByName(racoproject->project()->instances(), "Prefab")->as<user_types::Prefab>();

	EXPECT_EQ(node->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({meshnode}));
	EXPECT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({nodePrefab}));

	auto animation = core::Queries::findByName(racoproject->project()->instances(), "Animation")->as<user_types::Animation>();
	auto animationChannel = core::Queries::findByName(racoproject->project()->instances(), "AnimationChannel")->as<user_types::AnimationChannel>();
	auto skin = core::Queries::findByName(racoproject->project()->instances(), "Skin")->as<user_types::Skin>();

	auto renderBuffer = core::Queries::findByName(racoproject->project()->instances(), "RenderBuffer")->as<user_types::RenderBuffer>();
	auto renderBufferMS = core::Queries::findByName(racoproject->project()->instances(), "RenderBufferMS")->as<user_types::RenderBufferMS>();
	auto renderLayer = core::Queries::findByName(racoproject->project()->instances(), "MainRenderLayer")->as<user_types::RenderLayer>();

	auto renderTarget = core::Queries::findByName(racoproject->project()->instances(), "RenderTarget")->as<user_types::RenderTarget>();
	auto renderPass = core::Queries::findByName(racoproject->project()->instances(), "MainRenderPass")->as<user_types::RenderPass>();

	EXPECT_EQ(animation->animationChannels_->asVector<user_types::SAnimationChannel>(), 
		std::vector<user_types::SAnimationChannel>({animationChannel, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(skin->targets_->asVector<user_types::SMeshNode>(), std::vector<user_types::SMeshNode>({meshnode}));

	EXPECT_EQ(renderTarget->buffers_->asVector<user_types::SRenderBuffer>(), 
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));

	EXPECT_EQ(renderPass->layers_->asVector<user_types::SRenderLayer>(), 
		std::vector<user_types::SRenderLayer>({renderLayer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
}

TEST_F(MigrationTest, migrate_from_V55) {
	using namespace raco;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V55.rca").string()));

	auto renderBuffer = core::Queries::findByName(racoproject->project()->instances(), "RenderBuffer")->as<user_types::RenderBuffer>();
	auto renderBufferMS = core::Queries::findByName(racoproject->project()->instances(), "RenderBufferMS")->as<user_types::RenderBufferMS>();

	auto renderTarget = core::Queries::findByName(racoproject->project()->instances(), "RenderTarget")->as<user_types::RenderTarget>();
	auto renderTarget_MS = core::Queries::findByName(racoproject->project()->instances(), "RenderTarget_MS")->as<user_types::RenderTargetMS>();
	auto renderTarget_mixed = core::Queries::findByName(racoproject->project()->instances(), "RenderTarget_mixed")->as<user_types::RenderTarget>();

	auto mainRenderPass = core::Queries::findByName(racoproject->project()->instances(), "MainRenderPass")->as<user_types::RenderPass>();
	auto renderPass = core::Queries::findByName(racoproject->project()->instances(), "RenderPass")->as<user_types::RenderPass>();
	auto renderPass_MS = core::Queries::findByName(racoproject->project()->instances(), "RenderPass_MS")->as<user_types::RenderPass>();
	auto renderPass_mixed = core::Queries::findByName(racoproject->project()->instances(), "RenderPass_mixed")->as<user_types::RenderPass>();

	EXPECT_EQ(*mainRenderPass->target_, nullptr);
	EXPECT_EQ(*renderPass->target_, renderTarget);
	EXPECT_EQ(*renderPass_MS->target_, renderTarget_MS);
	EXPECT_EQ(*renderPass_mixed->target_, renderTarget_mixed);

	EXPECT_EQ(renderTarget->buffers_->asVector<user_types::SRenderBuffer>(),
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(renderTarget_MS->buffers_->asVector<user_types::SRenderBufferMS>(),
		std::vector<user_types::SRenderBufferMS>({renderBufferMS, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(renderTarget_mixed->buffers_->asVector<user_types::SRenderBuffer>(),
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));

	EXPECT_EQ(renderTarget->userTags_->asVector<std::string>(), std::vector<std::string>({"cat"}));
	EXPECT_EQ(renderTarget_MS->userTags_->asVector<std::string>(), std::vector<std::string>({"cat", "dog"}));
	EXPECT_EQ(renderTarget_mixed->userTags_->asVector<std::string>(), std::vector<std::string>({"dog"}));
}

// The following tests verify the migration and fixup of the split of the render targets in the ->V56 migration:
TEST_F(MigrationTest, migrate_from_V54_rendertarget_base) {
	using namespace raco;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V54-rendertarget-base.rca").string()));

	auto renderBuffer = core::Queries::findByName(racoproject->project()->instances(), "RenderBuffer")->as<user_types::RenderBuffer>();
	auto renderBufferMS = core::Queries::findByName(racoproject->project()->instances(), "RenderBufferMS")->as<user_types::RenderBufferMS>();

	auto renderTarget_normal = core::Queries::findByName(racoproject->project()->instances(), "rt-normal")->as<user_types::RenderTarget>();
	auto renderTarget_multi = core::Queries::findByName(racoproject->project()->instances(), "rt-multi")->as<user_types::RenderTargetMS>();
	auto renderTarget_mixed = core::Queries::findByName(racoproject->project()->instances(), "rt-mixed")->as<user_types::RenderTarget>();

	auto mainRenderPass = core::Queries::findByName(racoproject->project()->instances(), "MainRenderPass")->as<user_types::RenderPass>();
	auto renderPass_normal = core::Queries::findByName(racoproject->project()->instances(), "rp-normal")->as<user_types::RenderPass>();
	auto renderPass_multi = core::Queries::findByName(racoproject->project()->instances(), "rp-multi")->as<user_types::RenderPass>();
	auto renderPass_mixed = core::Queries::findByName(racoproject->project()->instances(), "rp-mixed")->as<user_types::RenderPass>();

	EXPECT_EQ(*mainRenderPass->target_, nullptr);
	EXPECT_EQ(*renderPass_normal->target_, renderTarget_normal);
	EXPECT_EQ(*renderPass_multi->target_, renderTarget_multi);
	EXPECT_EQ(*renderPass_mixed->target_, renderTarget_mixed);

	EXPECT_EQ(renderTarget_normal->buffers_->asVector<user_types::SRenderBuffer>(),
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(renderTarget_multi->buffers_->asVector<user_types::SRenderBufferMS>(),
		std::vector<user_types::SRenderBufferMS>({renderBufferMS, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(renderTarget_mixed->buffers_->asVector<user_types::SRenderBuffer>(),
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
}

TEST_F(MigrationTest, migrate_from_V54_rendertarget_extref) {
	using namespace raco;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V54-rendertarget-extref.rca").string()));

	auto renderBuffer = core::Queries::findByName(racoproject->project()->instances(), "RenderBuffer")->as<user_types::RenderBuffer>();
	auto renderBufferMS = core::Queries::findByName(racoproject->project()->instances(), "RenderBufferMS")->as<user_types::RenderBufferMS>();

	auto renderTarget_normal = core::Queries::findByName(racoproject->project()->instances(), "rt-normal")->as<user_types::RenderTarget>();
	auto renderTarget_multi = core::Queries::findByName(racoproject->project()->instances(), "rt-multi")->as<user_types::RenderTargetMS>();
	auto renderTarget_mixed = core::Queries::findByName(racoproject->project()->instances(), "rt-mixed")->as<user_types::RenderTarget>();
	EXPECT_TRUE(renderTarget_normal->query<core::ExternalReferenceAnnotation>() != nullptr);
	EXPECT_TRUE(renderTarget_multi->query<core::ExternalReferenceAnnotation>() != nullptr);
	EXPECT_TRUE(renderTarget_mixed->query<core::ExternalReferenceAnnotation>() != nullptr);

	auto mainRenderPass = core::Queries::findByName(racoproject->project()->instances(), "MainRenderPass")->as<user_types::RenderPass>();
	auto renderPass_normal = core::Queries::findByName(racoproject->project()->instances(), "rp-normal")->as<user_types::RenderPass>();
	auto renderPass_multi = core::Queries::findByName(racoproject->project()->instances(), "rp-multi")->as<user_types::RenderPass>();
	auto renderPass_mixed = core::Queries::findByName(racoproject->project()->instances(), "rp-mixed")->as<user_types::RenderPass>();

	EXPECT_EQ(*mainRenderPass->target_, nullptr);
	EXPECT_EQ(*renderPass_normal->target_, renderTarget_normal);
	EXPECT_EQ(*renderPass_multi->target_, renderTarget_multi);
	EXPECT_EQ(*renderPass_mixed->target_, renderTarget_mixed);

	EXPECT_EQ(renderTarget_normal->buffers_->asVector<user_types::SRenderBuffer>(),
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(renderTarget_multi->buffers_->asVector<user_types::SRenderBufferMS>(),
		std::vector<user_types::SRenderBufferMS>({renderBufferMS, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(renderTarget_mixed->buffers_->asVector<user_types::SRenderBuffer>(),
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
}

TEST_F(MigrationTest, migrate_from_V54_rendertarget_extref_keepalive) {
	using namespace raco;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V54-rendertarget-extref-keepalive.rca").string()));

	auto renderBuffer = core::Queries::findByName(racoproject->project()->instances(), "RenderBuffer")->as<user_types::RenderBuffer>();
	auto renderBufferMS = core::Queries::findByName(racoproject->project()->instances(), "RenderBufferMS")->as<user_types::RenderBufferMS>();

	auto renderTarget_normal = core::Queries::findByName(racoproject->project()->instances(), "rt-normal")->as<user_types::RenderTarget>();
	auto renderTarget_multi = core::Queries::findByName(racoproject->project()->instances(), "rt-multi")->as<user_types::RenderTargetMS>();
	auto renderTarget_mixed = core::Queries::findByName(racoproject->project()->instances(), "rt-mixed")->as<user_types::RenderTarget>();
	EXPECT_TRUE(renderTarget_normal->query<core::ExternalReferenceAnnotation>() != nullptr);
	EXPECT_TRUE(renderTarget_multi->query<core::ExternalReferenceAnnotation>() != nullptr);
	EXPECT_TRUE(renderTarget_mixed->query<core::ExternalReferenceAnnotation>() != nullptr);

	auto mainRenderPass = core::Queries::findByName(racoproject->project()->instances(), "MainRenderPass")->as<user_types::RenderPass>();
	auto renderPass_normal = core::Queries::findByName(racoproject->project()->instances(), "rp-normal")->as<user_types::RenderPass>();
	auto renderPass_multi = core::Queries::findByName(racoproject->project()->instances(), "rp-multi")->as<user_types::RenderPass>();
	auto renderPass_mixed = core::Queries::findByName(racoproject->project()->instances(), "rp-mixed")->as<user_types::RenderPass>();

	EXPECT_EQ(*mainRenderPass->target_, nullptr);
	EXPECT_EQ(*renderPass_normal->target_, renderTarget_normal);
	EXPECT_EQ(*renderPass_multi->target_, renderTarget_multi);
	EXPECT_EQ(*renderPass_mixed->target_, renderTarget_mixed);

	EXPECT_EQ(renderTarget_normal->buffers_->asVector<user_types::SRenderBuffer>(),
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(renderTarget_multi->buffers_->asVector<user_types::SRenderBufferMS>(),
		std::vector<user_types::SRenderBufferMS>({renderBufferMS, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(renderTarget_mixed->buffers_->asVector<user_types::SRenderBuffer>(),
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
}


TEST_F(MigrationTest, migrate_from_V57) {
	using namespace raco;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V57.rca").string()));

	auto settings = racoproject->project()->settings();
	auto node = core::Queries::findByName(racoproject->project()->instances(), "Node")->as<user_types::Node>();
	auto prefab = core::Queries::findByName(racoproject->project()->instances(), "Prefab")->as<user_types::Prefab>();

	EXPECT_FALSE(settings->hasProperty("userTags"));
	EXPECT_EQ(node->userTags_->asVector<std::string>(), std::vector<std::string>({"dog"}));
	EXPECT_EQ(prefab->userTags_->asVector<std::string>(), std::vector<std::string>({"cat", "dog"}));
}

TEST_F(MigrationTest, migrate_from_V58) {
	using namespace raco;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V58.rca").string()));

	auto node = core::Queries::findByName(racoproject->project()->instances(), "Node")->as<user_types::Node>();
	auto meshnode = core::Queries::findByName(racoproject->project()->instances(), "MeshNode")->as<user_types::MeshNode>();

	auto animation = core::Queries::findByName(racoproject->project()->instances(), "Animation")->as<user_types::Animation>();
	auto animationChannel = core::Queries::findByName(racoproject->project()->instances(), "AnimationChannel")->as<user_types::AnimationChannel>();
	auto skin = core::Queries::findByName(racoproject->project()->instances(), "Skin")->as<user_types::Skin>();

	auto renderBuffer = core::Queries::findByName(racoproject->project()->instances(), "RenderBuffer")->as<user_types::RenderBuffer>();
	auto renderBufferMS = core::Queries::findByName(racoproject->project()->instances(), "RenderBufferMS")->as<user_types::RenderBufferMS>();
	auto renderLayer = core::Queries::findByName(racoproject->project()->instances(), "MainRenderLayer")->as<user_types::RenderLayer>();

	auto renderTarget = core::Queries::findByName(racoproject->project()->instances(), "RenderTarget")->as<user_types::RenderTarget>();
	auto renderTargetMS = core::Queries::findByName(racoproject->project()->instances(), "RenderTargetMS")->as<user_types::RenderTargetMS>();
	auto renderPass = core::Queries::findByName(racoproject->project()->instances(), "MainRenderPass")->as<user_types::RenderPass>();

	EXPECT_EQ(animation->animationChannels_->asVector<user_types::SAnimationChannel>(),
		std::vector<user_types::SAnimationChannel>({animationChannel, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(skin->targets_->asVector<user_types::SMeshNode>(), std::vector<user_types::SMeshNode>({meshnode}));

	EXPECT_EQ(renderTarget->buffers_->asVector<user_types::SRenderBuffer>(),
		std::vector<user_types::SRenderBuffer>({renderBuffer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
	EXPECT_EQ(renderTargetMS->buffers_->asVector<user_types::SRenderBufferMS>(),
		std::vector<user_types::SRenderBufferMS>({renderBufferMS, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));

	EXPECT_EQ(renderPass->layers_->asVector<user_types::SRenderLayer>(),
		std::vector<user_types::SRenderLayer>({renderLayer, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr}));
}

TEST_F(MigrationTest, migrate_from_V59) {
	using namespace raco;

	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V59.rca").string()));

	auto intf_scalar = core::Queries::findByName(racoproject->project()->instances(), "intf-scalar")->as<user_types::LuaInterface>();
	auto intf_array = core::Queries::findByName(racoproject->project()->instances(), "intf-array")->as<user_types::LuaInterface>();
	auto intf_struct = core::Queries::findByName(racoproject->project()->instances(), "intf-struct")->as<user_types::LuaInterface>();

	auto texture = core::Queries::findByName(racoproject->project()->instances(), "texture");

	auto node = core::Queries::findByName(racoproject->project()->instances(), "Node");
	auto meshnode_no_mat = core::Queries::findByName(racoproject->project()->instances(), "meshnode_no_mat");

	auto mat_scalar = core::Queries::findByName(racoproject->project()->instances(), "mat_scalar")->as<user_types::Material>();
	EXPECT_EQ(ValueHandle(mat_scalar, {"uniforms", "i"}).asInt(), 2);
	checkVec2iValue(ValueHandle(mat_scalar, {"uniforms", "iv2"}), {1, 2});

	auto mat_array_link_array = core::Queries::findByName(racoproject->project()->instances(), "mat_array_link_array")->as<user_types::Material>();
	EXPECT_EQ(ValueHandle(mat_array_link_array, {"uniforms", "ivec", "1"}).asInt(), 1);
	EXPECT_EQ(ValueHandle(mat_array_link_array, {"uniforms", "ivec", "2"}).asInt(), 2);

	auto mat_array_link_member = core::Queries::findByName(racoproject->project()->instances(), "mat_array_link_member")->as<user_types::Material>();

	auto mat_struct = core::Queries::findByName(racoproject->project()->instances(), "mat_struct")->as<user_types::Material>();
	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_prims", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "s_prims", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_samplers", "s_texture"}).asRef(), texture);

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "nested", "prims", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "nested", "prims", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "a_s_prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "a_s_prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_a_struct_prim", "prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "s_a_struct_prim", "prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "a_s_a_struct_prim", "1", "prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "a_s_a_struct_prim", "1", "prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_a_prims", "ivec", "1"}).asInt(), 1);
	EXPECT_EQ(ValueHandle(mat_struct, {"uniforms", "s_a_prims", "ivec", "2"}).asInt(), 2);
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "s_a_prims", "aivec2", "1"}), {1, 2});
	checkVec2iValue(ValueHandle(mat_struct, {"uniforms", "s_a_prims", "aivec2", "2"}), {3, 4});

	auto meshnode_mat_scalar = core::Queries::findByName(racoproject->project()->instances(), "meshnode_mat_scalar");
	EXPECT_EQ(ValueHandle(meshnode_mat_scalar, {"materials", "material", "uniforms", "i"}).asInt(), 2);
	checkVec2iValue(ValueHandle(meshnode_mat_scalar, {"materials", "material", "uniforms", "iv2"}), {1, 2});

	auto meshnode_mat_array_link_array = core::Queries::findByName(racoproject->project()->instances(), "meshnode_mat_array_link_array");
	EXPECT_EQ(ValueHandle(meshnode_mat_array_link_array, {"materials", "material", "uniforms", "ivec", "1"}).asInt(), 1);
	EXPECT_EQ(ValueHandle(meshnode_mat_array_link_array, {"materials", "material", "uniforms", "ivec", "2"}).asInt(), 2);

	auto meshnode_mat_array_link_member = core::Queries::findByName(racoproject->project()->instances(), "meshnode_mat_array_link_member");

	auto meshnode_mat_struct = core::Queries::findByName(racoproject->project()->instances(), "meshnode_mat_struct");
	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_prims", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_prims", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_samplers", "s_texture"}).asRef(), texture);

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "nested", "prims", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "nested", "prims", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_struct_prim", "prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_struct_prim", "prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_a_struct_prim", "1", "prims", "1", "i"}).asInt(), 42);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_a_struct_prim", "1", "prims", "1", "iv2"}), {1, 2});

	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "ivec", "1"}).asInt(), 1);
	EXPECT_EQ(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "ivec", "2"}).asInt(), 2);
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "aivec2", "1"}), {1, 2});
	checkVec2iValue(ValueHandle(meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "aivec2", "2"}), {3, 4});

	checkLinks(*racoproject->project(),
		{{{intf_scalar, {"inputs", "bool"}}, {node, {"visibility"}}, true, false},
			{{intf_scalar, {"inputs", "bool"}}, {meshnode_no_mat, {"visibility"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_scalar, {"uniforms", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector2f"}}, {mat_scalar, {"uniforms", "v2"}}, true, false},
			{{intf_array, {"inputs", "fvec"}}, {mat_array_link_array, {"uniforms", "fvec"}}, true, false},
			{{intf_scalar, {"inputs", "float"}}, {mat_array_link_member, {"uniforms", "fvec", "5"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_array_link_member, {"uniforms", "avec3", "2"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "s_prims", "f"}}, true, false},

			{{intf_array, {"inputs", "fvec"}}, {mat_struct, {"uniforms", "s_a_prims", "fvec"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "s_a_prims", "avec3", "1"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "nested", "prims", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "nested", "prims", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "s_a_struct_prim", "prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "s_a_struct_prim", "prims", "1", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "a_s_prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "a_s_prims", "1", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {mat_struct, {"uniforms", "a_s_a_struct_prim", "1", "prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {mat_struct, {"uniforms", "a_s_a_struct_prim", "1", "prims", "1", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_scalar, {"materials", "material", "uniforms", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector2f"}}, {meshnode_mat_scalar, {"materials", "material", "uniforms", "v2"}}, true, false},
			{{intf_array, {"inputs", "fvec"}}, {meshnode_mat_array_link_array, {"materials", "material", "uniforms", "fvec"}}, true, false},
			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_array_link_member, {"materials", "material", "uniforms", "fvec", "5"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_array_link_member, {"materials", "material", "uniforms", "avec3", "2"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_prims", "f"}}, true, false},

			{{intf_array, {"inputs", "fvec"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "fvec"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_prims", "avec3", "1"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "nested", "prims", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "nested", "prims", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_struct_prim", "prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "s_a_struct_prim", "prims", "1", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_prims", "1", "v3"}}, true, false},

			{{intf_scalar, {"inputs", "float"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_a_struct_prim", "1", "prims", "1", "f"}}, true, false},
			{{intf_scalar, {"inputs", "vector3f"}}, {meshnode_mat_struct, {"materials", "material", "uniforms", "a_s_a_struct_prim", "1", "prims", "1", "v3"}}, true, false}

		});
}

TEST_F(MigrationTest, migrate_from_V60) {
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "V60.rca").string()));

	EXPECT_EQ(*racoproject->project()->settings()->featureLevel_, 1);

	auto layer = core::Queries::findByName(racoproject->project()->instances(), "RenderLayerManual")->as<user_types::RenderLayer>();
	auto anno_main = layer->renderableTags_->get("main")->query<LinkEndAnnotation>();
	EXPECT_TRUE(anno_main != nullptr);
	// migration to V2001 reset feature level to 1
	EXPECT_EQ(*anno_main->featureLevel_, 1);

	auto luaModule = core::Queries::findByName(racoproject->project()->instances(), "LuaScriptModule")->as<user_types::LuaScriptModule>();

	auto luaScript = core::Queries::findByName(racoproject->project()->instances(), "LuaScript")->as<user_types::LuaScript>();
	EXPECT_EQ(luaScript->luaModules_->get("coalas")->asRef(), luaModule);

	auto luaInterface= core::Queries::findByName(racoproject->project()->instances(), "LuaInterface")->as<user_types::LuaInterface>();
	EXPECT_EQ(luaInterface->luaModules_->get("coalas")->asRef(), luaModule);


	auto renderpass = core::Queries::findByName(racoproject->project()->instances(), "RenderPass")->as<user_types::RenderPass>();
	auto order_anno = renderpass->renderOrder_.query<LinkEndAnnotation>();
	EXPECT_EQ(*order_anno->featureLevel_, 1);
	auto clearColor_anno = renderpass->clearColor_.query<LinkEndAnnotation>();
	EXPECT_EQ(*clearColor_anno->featureLevel_, 1);
	auto enabled_anno = renderpass->enabled_.query<LinkEndAnnotation>();
	EXPECT_EQ(*enabled_anno->featureLevel_, 1);
	auto renderOnce_anno = renderpass->renderOnce_.query<LinkEndAnnotation>();
	EXPECT_EQ(*renderOnce_anno->featureLevel_, 1);

	auto meshnode = core::Queries::findByName(racoproject->project()->instances(), "meshnode_no_tex")->as<user_types::MeshNode>();
	auto instanceCount_anno = meshnode->instanceCount_.query<LinkEndAnnotation>();
	EXPECT_EQ(*instanceCount_anno->featureLevel_, 1);
	auto meshnode_enabled_anno = meshnode->enabled_.query<LinkEndAnnotation>();
	EXPECT_EQ(*meshnode_enabled_anno->featureLevel_, 1);
}

TEST_F(MigrationTest, migrate_from_current) {
	// Check for changes in serialized JSON in newest version.
	// Should detect changes in data model with missing migration code.
	// Also checks that all object types are present in file.
	//
	// The "version-current.rca" project needs to be updated when the data model has
	// been changed in a way that changes the serialized JSON, e.g.
	// - static properties added
	// - annotations added to static properties
	// - added new object types

	int fileVersion;
	auto racoproject = loadAndCheckJson(QString::fromStdString((test_path() / "migrationTestData" / "version-current.rca").string()), &fileVersion);
	ASSERT_EQ(fileVersion, serialization::RAMSES_PROJECT_FILE_VERSION);

	ASSERT_EQ(racoproject->project()->featureLevel(), static_cast<int>(ramses_base::BaseEngineBackend::maxFeatureLevel));

	// check that all user types present in file
	auto& instances = racoproject->project()->instances();
	for (auto& item : objectFactory()->getTypes()) {
		auto name = item.first;
		EXPECT_TRUE(std::find_if(instances.begin(), instances.end(), [name](SEditorObject obj) {
			return name == obj->getTypeDescription().typeName;
		}) != instances.end()) << fmt::format("Type '{}' missing in version-current.rca", name);
	}
}

TEST_F(MigrationTest, check_current_type_maps) {
	// Check that the type maps for user object and structs types in the "version-current.rca" are
	// identical to the ones generated when saving a project.
	//
	// If this fails the file format has changed: we need to increase the file version number
	// and potentially write migration code.

	QString filename = QString::fromStdString((test_path() / "migrationTestData" / "version-current.rca").string());
	QFile file{filename};
	EXPECT_TRUE(file.open(QIODevice::ReadOnly | QIODevice::Text));
	auto document{QJsonDocument::fromJson(file.readAll())};
	file.close();

	auto fileUserPropTypeMap = serialization::deserializeUserTypePropertyMap(document[serialization::keys::USER_TYPE_PROP_MAP]);
	auto fileStructTypeMap = serialization::deserializeUserTypePropertyMap(document[serialization::keys::STRUCT_PROP_MAP]);

	auto currentUserPropTypeMap = serialization::makeUserTypePropertyMap();
	auto currentStructTypeMap = serialization::makeStructPropertyMap();

	EXPECT_EQ(fileUserPropTypeMap, currentUserPropTypeMap);
	EXPECT_EQ(fileStructTypeMap, currentStructTypeMap);
}

TEST_F(MigrationTest, check_proxy_factory_has_all_objects_types) {
	// Check that all types in the UserObjectFactory constructory via makeTypeMap call
	// also have the corresponding proxy type added in the ProxyObjectFactory constructor.
	// If this fails add the type in the ProxyObjectFactory constructor makeTypeMap call.

	auto& proxyFactory{serialization::proxy::ProxyObjectFactory::getInstance()};
	auto& proxyTypeMap{proxyFactory.getTypes()};

	for (auto& item : objectFactory()->getTypes()) {
		auto name = item.first;
		EXPECT_TRUE(proxyTypeMap.find(name) != proxyTypeMap.end()) << fmt::format("type name: '{}'", name);
	}
}

TEST_F(MigrationTest, check_proxy_factory_has_all_dynamic_property_types) {
	// Check that all dynamic properties contained in UserObjectFactory::PropertyTypeMapType
	// have their corresponding properties added in ProxyObjectFactory::PropertyTypeMapType too.
	// If this fails add the property in ProxyObjectFactory::PropertyTypeMapType.

	auto& proxyFactory{serialization::proxy::ProxyObjectFactory::getInstance()};
	auto& userFactory{UserObjectFactory::getInstance()};
	auto& proxyProperties{proxyFactory.getProperties()};

	for (auto& item : userFactory.getProperties()) {
		auto name = item.first;
		EXPECT_TRUE(proxyProperties.find(name) != proxyProperties.end()) << fmt::format("property name: '{}'", name);
	}
}

TEST_F(MigrationTest, check_proxy_factory_can_create_all_static_properties) {
	// Check that the ProxyObjectFactory can create all statically known properties.
	// If this fails add the failing property to the ProxyObjectFactory::PropertyTypeMapType.

	auto& proxyFactory{serialization::proxy::ProxyObjectFactory::getInstance()};
	auto& userFactory{UserObjectFactory::getInstance()};

	for (auto& item : userFactory.getTypes()) {
		auto name = item.first;
		auto object = objectFactory()->createObject(name);
		ASSERT_TRUE(object != nullptr);
		for (size_t index = 0; index < object->size(); index++) {
			if (object->get(index)->query<VolatileProperty>()) {
				continue;
			}
			auto propTypeName = object->get(index)->typeName();
			auto proxyProperty = proxyFactory.createValue(propTypeName);
			ASSERT_TRUE(proxyProperty != nullptr) << fmt::format("property type name: '{}'", propTypeName);
			ASSERT_EQ(proxyProperty->typeName(), propTypeName) << fmt::format("property type name: '{}'", propTypeName);
		}
	}

	for (auto& item : userFactory.getStructTypes()) {
		auto name = item.first;
		auto object = userFactory.createStruct(name);
		ASSERT_TRUE(object != nullptr);
		for (size_t index = 0; index < object->size(); index++) {
			auto propTypeName = object->get(index)->typeName();
			auto proxyProperty = proxyFactory.createValue(propTypeName);
			ASSERT_TRUE(proxyProperty != nullptr) << fmt::format("property type name: '{}'", propTypeName);
			ASSERT_EQ(proxyProperty->typeName(), propTypeName) << fmt::format("property type name: '{}'", propTypeName);
		}
	}
}

TEST_F(MigrationTest, check_user_factory_can_create_all_static_properties) {
	// Check that the UserObjectFactory can create all statically known properties.
	// If this fails add the failing property to the UserObjectFactory::PropertyTypeMapType.

	auto& userFactory{UserObjectFactory::getInstance()};

	for (auto& item : userFactory.getTypes()) {
		auto name = item.first;
		auto object = objectFactory()->createObject(name);
		ASSERT_TRUE(object != nullptr);
		for (size_t index = 0; index < object->size(); index++) {
			if (object->get(index)->query<VolatileProperty>()) {
				continue;
			}
			auto propTypeName = object->get(index)->typeName();
			auto userProperty = userFactory.createValue(propTypeName);
			ASSERT_TRUE(userProperty != nullptr) << fmt::format("property type name: '{}'", propTypeName);
			ASSERT_EQ(userProperty->typeName(), propTypeName) << fmt::format("property type name: '{}'", propTypeName);
		}
	}

	for (auto& item : userFactory.getStructTypes()) {
		auto name = item.first;
		auto object = userFactory.createStruct(name);
		ASSERT_TRUE(object != nullptr);
		for (size_t index = 0; index < object->size(); index++) {
			auto propTypeName = object->get(index)->typeName();
			auto userProperty = userFactory.createValue(propTypeName);
			ASSERT_TRUE(userProperty != nullptr) << fmt::format("property type name: '{}'", propTypeName);
			ASSERT_EQ(userProperty->typeName(), propTypeName) << fmt::format("property type name: '{}'", propTypeName);
		}
	}
}

void change_property(ValueBase* value) {
	switch (value->type()) {
		case PrimitiveType::Bool:
			*value = !value->asBool();
			break;
		case PrimitiveType::Int:
			*value = value->asInt() + 1;
			break;
		case PrimitiveType::Int64:
			*value = value->asInt64() + 1;
			break;
		case PrimitiveType::Double:
			*value = value->asDouble() + 1;
			break;
		case PrimitiveType::String:
			*value = value->asString() + "postfix";
			break;
		case PrimitiveType::Ref:
			// We can't just change the pointer here but would need to create another valid object as pointer target.
			// Ignore for now since we don't have Ref properties inside structs yet.
			break;
		case PrimitiveType::Table:
		case PrimitiveType::Struct: {
			auto& container = value->getSubstructure();
			for (size_t index = 0; index < container.size(); index++) {
				change_property(container.get(index));
			}
		} break;
	}
}

TEST_F(MigrationTest, check_struct_copy_operators) {
	// Check that the copy constructor and operator= work for all struct types.
	// If this fails fix the implementation of the failing struct member function.

	auto& userFactory{UserObjectFactory::getInstance()};

	for (auto& item : userFactory.getStructTypes()) {
		auto name = item.first;
		auto property = userFactory.createValue(name);
		ASSERT_TRUE(property->type() != PrimitiveType::Ref) << fmt::format("Struct name {}", name);

		change_property(property);

		// clone uses the copy constructor
		auto prop_clone = property->clone(nullptr);
		ASSERT_TRUE(*property == *prop_clone) << fmt::format("Struct name {}", name);

		// check operator=
		auto property_2 = userFactory.createValue(name);
		ASSERT_FALSE(*property_2 == *property);

		*property_2 = *property;
		ASSERT_TRUE(*property_2 == *property) << fmt::format("Struct name {}", name);
	}
}