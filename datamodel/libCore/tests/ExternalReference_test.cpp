/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This file is part of Ramses Composer
 * (see https://github.com/GENIVI/ramses-composer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "core/Context.h"
#include "core/ExternalReferenceAnnotation.h"
#include "core/Handles.h"
#include "core/MeshCacheInterface.h"
#include "core/Project.h"
#include "core/Queries.h"
#include "ramses_base/HeadlessEngineBackend.h"
#include "testing/RacoBaseTest.h"
#include "testing/TestEnvironmentCore.h"
#include "testing/TestUtil.h"
#include "user_types/UserObjectFactory.h"
#include "application/RaCoProject.h"
#include "application/RaCoApplication.h"
#include "ramses_adaptor/SceneBackend.h"

#include "user_types/Animation.h"
#include "user_types/AnimationChannel.h"
#include "user_types/LuaScriptModule.h"
#include "user_types/Mesh.h"
#include "user_types/MeshNode.h"
#include "user_types/Node.h"
#include "user_types/Prefab.h"
#include "user_types/PrefabInstance.h"
#include "user_types/Texture.h"
#include "user_types/RenderPass.h"

#include "gtest/gtest.h"

#include "utils/stdfilesystem.h"

#include <algorithm>

using namespace raco::core;
using namespace raco::user_types;

using raco::application::RaCoApplication;

class ExtrefTest : public RacoBaseTest<> {
public:
	void checkLinks(const std::vector<raco::core::Link> &refLinks) {
		EXPECT_EQ(refLinks.size(), project->links().size());
		for (const auto &refLink : refLinks) {
			auto projectLink = raco::core::Queries::getLink(*project, refLink.endProp());
			EXPECT_TRUE(projectLink && projectLink->startProp() == refLink.startProp() && projectLink->isValid() == refLink.isValid());
		}
	}

	template <class C>
	std::shared_ptr<C> create(std::string name, SEditorObject parent = nullptr) {
		auto obj = std::dynamic_pointer_cast<C>(cmd->createObject(C::typeDescription.typeName, name));
		if (parent) {
			cmd->moveScenegraphChildren({obj}, parent);
		}
		return obj;
	}

	SAnimation create_animation(const std::string &name) {
		auto channel = create<Animation>(name);
		cmd->set({channel, {"play"}}, true);
		cmd->set({channel, {"loop"}}, true);
		return channel;
	}

	SAnimationChannel create_animationchannel(const std::string& name, const std::string& relpath) {
		auto channel = create<AnimationChannel>(name);
		cmd->set({channel, {"uri"}}, (test_path() / relpath).string());
		return channel;
	}

	SMesh create_mesh(const std::string &name, const std::string &relpath) {
		auto mesh = create<Mesh>(name);
		cmd->set({mesh, {"uri"}}, (test_path() / relpath).string());
		return mesh;
	}

	SMaterial create_material(const std::string &name, const std::string &relpathVertex, const std::string &relpathFragment) {
		auto material = create<Material>(name);
		cmd->set({material, {"uriVertex"}}, (test_path() / relpathVertex).string());
		cmd->set({material, {"uriFragment"}}, (test_path() / relpathFragment).string());
		return material;
	}

	SMeshNode create_meshnode(const std::string &name, SMesh mesh, SMaterial material, SEditorObject parent = nullptr) {
		auto meshnode = create<MeshNode>(name, parent);
		cmd->set({meshnode, {"mesh"}}, mesh);
		cmd->set({meshnode, {"materials", "material", "material"}}, material);
		return meshnode;
	}

	void change_uri(SEditorObject obj, const std::string &newvalue) {
		cmd->set({obj, {"uri"}}, (test_path() / newvalue).string());
	}

	void rename_project(const std::string &newProjectName) {
		if (!newProjectName.empty()) {
			cmd->set({app->activeRaCoProject().project()->settings(), {"objectName"}}, newProjectName);
		}
	}

	SEditorObject find(const std::string& name) {
		auto obj = Queries::findByName(project->instances(), name);
		EXPECT_TRUE(obj != nullptr);
		return obj;
	}

	void dontFind(const std::string &name) {
		auto obj = Queries::findByName(project->instances(), name);
		EXPECT_TRUE(obj == nullptr);
	}

	template<typename T = EditorObject>
	std::shared_ptr<T> findExt(const std::string& name, const std::string& projectID = std::string()) {
		SEditorObject editorObj = Queries::findByName(project->instances(), name);
		auto anno = editorObj->query<raco::core::ExternalReferenceAnnotation>();
		EXPECT_TRUE(anno != nullptr);
		if (!projectID.empty()) {
			EXPECT_EQ(*anno->projectID_, projectID);
		}
		auto objAsT = editorObj->as<T>();
		EXPECT_TRUE(objAsT != nullptr);
		return objAsT;
	}

	template<typename T = EditorObject>
	std::shared_ptr<T> findChild(SEditorObject object) {
		for (auto child : object->children_->asVector<SEditorObject>()) {
			if (auto childAsT = child->as<T>()) {
				return childAsT;
			}
		}
		return {};
	}

	template<typename T>
	size_t countInstances() {
		return std::count_if(project->instances().begin(), project->instances().end(), [](auto obj) {
			return std::dynamic_pointer_cast<T>(obj) != nullptr;
		});
	}

	raco::ramses_base::HeadlessEngineBackend backend{};

	RaCoApplication *app;
	Project *project;
	CommandInterface *cmd;

	std::string setupBase(const std::string &basePathName, std::function<void()> func, const std::string& baseProjectName = std::string("base")) {
		RaCoApplication base{backend};
		app = &base;
		project = base.activeRaCoProject().project();
		cmd = base.activeRaCoProject().commandInterface();

		rename_project(baseProjectName);

		func();

		base.activeRaCoProject().saveAs(basePathName.c_str());
		return project->projectID();
	}

	std::string setupGeneric(std::function<void()> func) {
		RaCoApplication app_{backend};
		app = &app_;
		project = app_.activeRaCoProject().project();
		cmd = app_.activeRaCoProject().commandInterface();

		func();
		return project->projectID();
	}

	 bool pasteFromExt(const std::string &basePathName, const std::vector<std::string> &externalObjectNames, bool asExtref, std::vector<SEditorObject> *outPasted = nullptr) {
		bool success;
		std::vector<std::string> stack;
		stack.emplace_back(project->currentPath());
		bool status = app->externalProjects()->addExternalProject(basePathName.c_str(), stack);
		if (!status) {
			if (outPasted) {
				outPasted->clear();
			}
			return false;
		}
		auto originProject = app->externalProjects()->getExternalProject(basePathName);
		auto originCmd = app->externalProjects()->getExternalProjectCommandInterface(basePathName);

		std::vector<SEditorObject> origin;
		for (auto name : externalObjectNames) {
			if (auto obj = Queries::findByName(originProject->instances(), name)) {
				origin.emplace_back(obj);
			}
		}

		auto pasted = cmd->pasteObjects(originCmd->copyObjects(origin), nullptr, asExtref, &success);
		if (outPasted) {
			*outPasted = pasted;
		}
		return success;
	}

	std::string setupComposite(const std::string &basePathName, const std::string &compositePathName, const std::vector<std::string> &externalObjectNames,
		std::function<void()> func, const std::string& projectName = std::string()) {
		RaCoApplication app_{backend};
		app = &app_;
		project = app->activeRaCoProject().project();
		cmd = app->activeRaCoProject().commandInterface();

		rename_project(projectName);

		pasteFromExt(basePathName, externalObjectNames, true);

		func();

		if (!compositePathName.empty()) {
			app->activeRaCoProject().saveAs(compositePathName.c_str());
		}
		return project->projectID();
	}

	void updateBase(const std::string &basePathName, std::function<void()> func) {
		RaCoApplication base{backend, basePathName.c_str()};
		app = &base;
		project = base.activeRaCoProject().project();
		cmd = base.activeRaCoProject().commandInterface();

		func();

		ASSERT_TRUE(base.activeRaCoProject().save());
	}

	void updateComposite(const std::string &pathName, std::function<void()> func) {
		RaCoApplication app_{backend, pathName.c_str()};
		app = &app_;
		project = app->activeRaCoProject().project();
		cmd = app->activeRaCoProject().commandInterface();

		func();
	}
};


class ExtrefTestWithZips : public ExtrefTest {
public:
	std::string setupBase(const std::string &basePathName, std::function<void()> func, const std::string &baseProjectName = std::string("base")) {
		RaCoApplication base{backend};
		app = &base;
		project = base.activeRaCoProject().project();
		cmd = base.activeRaCoProject().commandInterface();

		rename_project(baseProjectName);

		func();

		cmd->set({base.activeRaCoProject().project()->settings(), &raco::user_types::ProjectSettings::saveAsZip_}, true);		
		base.activeRaCoProject().saveAs(basePathName.c_str());
		return project->projectID();
	}

	std::string setupGeneric(std::function<void()> func) {
		RaCoApplication app_{backend};
		app = &app_;
		project = app_.activeRaCoProject().project();
		cmd = app_.activeRaCoProject().commandInterface();

		func();
		cmd->set({app_.activeRaCoProject().project()->settings(), &raco::user_types::ProjectSettings::saveAsZip_}, true);
		return project->projectID();
	}

	std::string setupComposite(const std::string &basePathName, const std::string &compositePathName, const std::vector<std::string> &externalObjectNames,
		std::function<void()> func, const std::string &projectName = std::string()) {
		RaCoApplication app_{backend};
		app = &app_;
		project = app->activeRaCoProject().project();
		cmd = app->activeRaCoProject().commandInterface();

		rename_project(projectName);

		pasteFromExt(basePathName, externalObjectNames, true);

		func();

		cmd->set({app_.activeRaCoProject().project()->settings(), &raco::user_types::ProjectSettings::saveAsZip_}, true);
		if (!compositePathName.empty()) {
			app->activeRaCoProject().saveAs(compositePathName.c_str());
		}
		return project->projectID();
	}

};


TEST_F(ExtrefTest, normal_paste) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("Prefab");
		auto node = create<Node>("prefab_child", prefab);
	});

	setupGeneric([this, basePathName]() {
		pasteFromExt(basePathName, {"Prefab"}, false);

		auto prefab = find("Prefab");
		auto node = find("prefab_child");
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node}));
	});
}

TEST_F(ExtrefTest, duplicate_normal_paste) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("Prefab");
	});

	setupGeneric([this, basePathName]() {
		std::vector<std::string> stack;
		stack.emplace_back(project->currentPath());
		app->externalProjects()->addExternalProject(basePathName.c_str(), stack);
		auto originProject = app->externalProjects()->getExternalProject(basePathName);
		auto originCmd = app->externalProjects()->getExternalProjectCommandInterface(basePathName);
		auto originPrefab = Queries::findByName(originProject->instances(), "Prefab");

		auto pasted1 = cmd->pasteObjects(originCmd->copyObjects({originPrefab}));
		auto pasted2 = cmd->pasteObjects(originCmd->copyObjects({originPrefab}));

		auto prefab1 = find("Prefab");
		auto prefab2 = find("Prefab (1)");
	});
}


TEST_F(ExtrefTest, extref_paste_empty_projectname) {
	auto basePathName{(test_path() / "base.rcp").string()};

	auto base_id = setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("Prefab");
	}, std::string());
	
	setupGeneric([this, basePathName, base_id]() {
		ASSERT_TRUE(pasteFromExt(basePathName, {"Prefab"}, true));

		ASSERT_TRUE(project->hasExternalProjectMapping(base_id));
	});
}


TEST_F(ExtrefTest, extref_paste_fail_renderpass) {
	auto basePathName{(test_path() / "base.rca").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<RenderPass>("renderpass");
	});

	setupGeneric([this, basePathName]() {
		std::vector<SEditorObject> pasted;
		ASSERT_TRUE(pasteFromExt(basePathName, {"renderpass"}, true, &pasted));
		ASSERT_EQ(pasted.size(), 0);
	});
}

TEST_F(ExtrefTest, extref_paste_fail_existing_object_from_same_project) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("Prefab");
	});

	updateComposite(basePathName, [this, basePathName]() {
		ASSERT_FALSE(pasteFromExt(basePathName, {"Prefab"}, true));
	});
}

TEST_F(ExtrefTest, extref_paste_fail_deleted_object_from_same_project) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("Prefab");
	});

	updateComposite(basePathName, [this, basePathName]() {
		auto prefab = find("Prefab");
		cmd->deleteObjects({prefab});
		dontFind("Prefab");

		ASSERT_FALSE(pasteFromExt(basePathName, {"Prefab"}, true));
	});
}

TEST_F(ExtrefTest, extref_paste_fail_existing_object_from_same_project_path) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("Prefab");
	});

	updateComposite(basePathName, [this, basePathName]() {
		rename_project("not_base_anymore");
		ASSERT_FALSE(pasteFromExt(basePathName, {"Prefab"}, true));
	});
}

TEST_F(ExtrefTest, extref_paste_fail_deleted_object_from_same_project_path) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("Prefab");
	});

	updateComposite(basePathName, [this, basePathName]() {
		rename_project("not_base_anymore");

		auto prefab = find("Prefab");
		cmd->deleteObjects({prefab});
		dontFind("Prefab");

		ASSERT_FALSE(pasteFromExt(basePathName, {"Prefab"}, true));
	});
}

TEST_F(ExtrefTest, extref_paste_fail_from_filecopy) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto mesh = create<Mesh>("mesh");
	}, std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		auto mesh = find("mesh");
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create<MeshNode>("meshnode", prefab);
		cmd->set({meshnode, {"mesh"}}, mesh);
		auto material = create<Material>("material");
	});

	updateComposite(basePathName1, [this, basePathName2]() {
		ASSERT_FALSE(pasteFromExt(basePathName2, {"mesh"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"material"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"prefab"}, true));
	});

	setupGeneric([this, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName2, {"mesh"}, true));
		ASSERT_TRUE(pasteFromExt(basePathName2, {"material"}, true));
		ASSERT_TRUE(pasteFromExt(basePathName2, {"prefab"}, true));
	});
}

TEST_F(ExtrefTest, extref_paste) {
	auto basePathName{(test_path() / "base.rcp").string()};

	auto base_id = setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_child", prefab);
	});

	setupComposite(basePathName, "", {"prefab"}, [this, basePathName, base_id]() {
		auto prefab = findExt("prefab");
		auto node = findExt("prefab_child");
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node}));

		ASSERT_EQ(project->lookupExternalProjectPath(base_id), basePathName);
	});
}

TEST_F(ExtrefTest, extref_paste_duplicate_projname) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	auto base1_id = setupBase(basePathName1, [this]() {
		auto prefab = create<Prefab>("Prefab");
	}, std::string("base"));

	auto base2_id = setupBase(basePathName2, [this]() {
		auto mesh = create<Mesh>("mesh");
	}, std::string("base"));

	setupGeneric([this, basePathName1, basePathName2, base1_id, base2_id]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"Prefab"}, true));
		ASSERT_EQ(project->lookupExternalProjectPath(base1_id), basePathName1);
		ASSERT_TRUE(pasteFromExt(basePathName2, {"mesh"}, true));
		ASSERT_EQ(project->lookupExternalProjectPath(base2_id), basePathName2);
	});
}

TEST_F(ExtrefTest, filecopy_paste_fail_same_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto mesh = create<Mesh>("mesh");
	}, std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		rename_project("copy");
	});

	setupGeneric([this, basePathName1, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"mesh"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"mesh"}, true));
	});
}

TEST_F(ExtrefTest, filecopy_paste_fail_different_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto mesh = create<Mesh>("mesh");
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create<MeshNode>("meshnode", prefab);
	}, std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		rename_project("copy");
	});

	setupGeneric([this, basePathName1, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"prefab"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"mesh"}, true));
	});
}

TEST_F(ExtrefTest, filecopy_paste_fail_new_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto mesh = create<Mesh>("mesh");
	}, std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		auto material = create<Material>("material");
		rename_project("copy");
	});

	setupGeneric([this, basePathName1, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"mesh"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"material"}, true));
	});
}


TEST_F(ExtrefTest, extref_paste_same_project_name_after_delete_with_undo) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	auto base_id1 = setupBase(basePathName1, [this]() {
		auto prefab = create<Prefab>("Prefab");
	}, std::string("base"));

	auto base_id2 = setupBase(basePathName2, [this]() {
		auto mesh = create<Mesh>("mesh");
	}, std::string("base"));

	setupGeneric([this, basePathName1, basePathName2, base_id1, base_id2]() {
		std::vector<SEditorObject> pasted;

		checkUndoRedoMultiStep<3>(*cmd,
			{[this, basePathName1, &pasted]() {
				 ASSERT_TRUE(pasteFromExt(basePathName1, {"Prefab"}, true, &pasted));
			 },
				[this, basePathName1, base_id1, &pasted]() {
					cmd->deleteObjects(pasted);
					ASSERT_FALSE(project->hasExternalProjectMapping(base_id1));
				},
				[this, basePathName2, &pasted]() {
					ASSERT_TRUE(pasteFromExt(basePathName2, {"mesh"}, true));
				}},
			{[this, base_id1, base_id2]() {
				 ASSERT_FALSE(project->hasExternalProjectMapping(base_id1));
				 ASSERT_FALSE(project->hasExternalProjectMapping(base_id2));
			 },
				[this, basePathName1, basePathName2, base_id1, base_id2]() {
					ASSERT_EQ(project->lookupExternalProjectPath(base_id1), basePathName1);
					ASSERT_FALSE(project->hasExternalProjectMapping(base_id2));
				},
				[this, basePathName1, basePathName2, base_id1, base_id2]() {
					ASSERT_FALSE(project->hasExternalProjectMapping(base_id1));
					ASSERT_FALSE(project->hasExternalProjectMapping(base_id2));
				},
				[this, basePathName1, basePathName2, base_id1, base_id2]() {
					ASSERT_FALSE(project->hasExternalProjectMapping(base_id1));
					ASSERT_EQ(project->lookupExternalProjectPath(base_id2), basePathName2);
				}});
	});
}

TEST_F(ExtrefTest, duplicate_extref_paste_discard_all) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("Prefab");
	});

	setupGeneric([this, basePathName]() {
		std::vector<std::string> stack;
		stack.emplace_back(project->currentPath());
		app->externalProjects()->addExternalProject(basePathName.c_str(), stack);
		auto originProject = app->externalProjects()->getExternalProject(basePathName);
		auto originCmd = app->externalProjects()->getExternalProjectCommandInterface(basePathName);
		auto originPrefab = Queries::findByName(originProject->instances(), "Prefab");

		auto pasted1 = cmd->pasteObjects(originCmd->copyObjects({originPrefab}), nullptr, true);
		auto pasted2 = cmd->pasteObjects(originCmd->copyObjects({originPrefab}), nullptr, true);

		int num = std::count_if(project->instances().begin(), project->instances().end(), [](SEditorObject obj) {
			return obj->objectName() == "Prefab";
		});
		ASSERT_EQ(num, 1);
	});
}

TEST_F(ExtrefTest, duplicate_extref_paste_discard_some) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto mesh = create<Mesh>("Mesh");
		auto prefab = create<Prefab>("Prefab");
		auto meshnode = create<MeshNode>("prefab_child", prefab);
		cmd->set({meshnode, {"mesh"}}, mesh);
	});

	setupGeneric([this, basePathName]() {
		std::vector<std::string> stack;
		stack.emplace_back(project->currentPath());
		app->externalProjects()->addExternalProject(basePathName.c_str(), stack);
		auto originProject = app->externalProjects()->getExternalProject(basePathName);
		auto originCmd = app->externalProjects()->getExternalProjectCommandInterface(basePathName);
		auto originMesh = Queries::findByName(originProject->instances(), "Mesh");
		auto originPrefab = Queries::findByName(originProject->instances(), "Prefab");

		auto pasted1 = cmd->pasteObjects(originCmd->copyObjects({originMesh}), nullptr, true);
		auto mesh = findExt("Mesh");

		auto pasted2 = cmd->pasteObjects(originCmd->copyObjects({originPrefab}), nullptr, true);

		int num = std::count_if(project->instances().begin(), project->instances().end(), [](SEditorObject obj) {
			return obj->objectName() == "Mesh";
		});
		ASSERT_EQ(num, 1);
		auto prefab = findExt("Prefab");
		auto meshnode = findExt<MeshNode>("prefab_child");
		ASSERT_TRUE(meshnode != nullptr);
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({meshnode}));
		ASSERT_EQ(*meshnode->mesh_, mesh);
	});
}

TEST_F(ExtrefTest, extref_projname_change_update) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
		auto prefab = findExt("prefab");
	});

	updateBase(basePathName, [this]() {
		rename_project("Foo");
	});

	updateComposite(compositePathName, [this]() {
		auto prefab = findExt("prefab");
		auto anno = prefab->query<ExternalReferenceAnnotation>();
		std::vector<std::string> stack;
		stack.emplace_back(project->currentPath());
		auto extProject = app->externalProjects()->addExternalProject(project->lookupExternalProjectPath(*anno->projectID_), stack);
		ASSERT_EQ(extProject->projectName(), std::string("Foo"));
	});
}

TEST_F(ExtrefTest, extref_projname_change_paste_more) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto mesh = create<Mesh>("mesh");
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
		auto prefab = findExt("prefab");
	});

	updateBase(basePathName, [this]() {
		rename_project("Foo");
	});

	updateComposite(compositePathName, [this, basePathName]() {
		ASSERT_TRUE(pasteFromExt(basePathName, {"mesh"}, true));

		std::vector<std::string> stack;
		stack.emplace_back(project->currentPath());
		for (auto obj : project->instances()) {
			if (auto anno = obj->query<ExternalReferenceAnnotation>()) {
				auto extProject = app->externalProjects()->addExternalProject(project->lookupExternalProjectPath(*anno->projectID_), stack);
				ASSERT_EQ(extProject->projectName(), std::string("Foo"));
			}
		}
	});
}

TEST_F(ExtrefTest, extref_can_delete_only_unused) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto mesh = create<Mesh>("mesh");
		auto material = create<Material>("material");
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create<MeshNode>("prefab_meshnode", prefab);
		cmd->set({meshnode, {"mesh"}}, mesh);
	});

	setupComposite(basePathName, compositePathName, {"prefab", "material"}, [this]() {
		auto prefab = findExt("prefab");
		auto material = findExt("material");
		auto mesh = findExt("mesh");
		auto meshnode = findExt("prefab_meshnode");

		EXPECT_FALSE(Queries::filterForDeleteableObjects(*project, {material}).empty());
		EXPECT_TRUE(Queries::filterForDeleteableObjects(*project, {mesh}).empty());
		EXPECT_FALSE(Queries::filterForDeleteableObjects(*project, {prefab}).empty());

		EXPECT_EQ(Queries::filterForDeleteableObjects(*project, {material, mesh}).size(), 1);
		EXPECT_EQ(Queries::filterForDeleteableObjects(*project, {prefab, mesh}).size(), 3);
		EXPECT_EQ(Queries::filterForDeleteableObjects(*project, {prefab, meshnode}).size(), 2);
		EXPECT_EQ(Queries::filterForDeleteableObjects(*project, {prefab, mesh, meshnode}).size(), 3);
		EXPECT_EQ(Queries::filterForDeleteableObjects(*project, {prefab, meshnode, mesh, material}).size(), 4);
		EXPECT_EQ(Queries::filterForDeleteableObjects(*project, {prefab, material}).size(), 3);

		cmd->deleteObjects({prefab});

		EXPECT_FALSE(Queries::filterForDeleteableObjects(*project, {material}).empty());
		EXPECT_FALSE(Queries::filterForDeleteableObjects(*project, {mesh}).empty());
	});
}

TEST_F(ExtrefTest, extref_can_delete_only_unused_with_links) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto luaSource = create<LuaScript>("luaSource");
		auto luaSink = create<LuaScript>("luaSink");

		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.v = VEC3F
	OUT.v = VEC3F
end
function run()
end
)");
		cmd->set({luaSource, {"uri"}}, scriptFile);
		cmd->set({luaSink, {"uri"}}, scriptFile);
		cmd->addLink({luaSource, {"luaOutputs", "v"}}, {luaSink, {"luaInputs", "v"}});
	});

	setupComposite(basePathName, compositePathName, {"luaSink"}, [this]() {
		auto luaSource = findExt("luaSource");
		auto luaSink = findExt("luaSink");

		EXPECT_FALSE(Queries::filterForDeleteableObjects(*project, {luaSink}).empty());
		EXPECT_EQ(Queries::filterForDeleteableObjects(*project, {luaSink, luaSource}).size(), 2);
		EXPECT_TRUE(Queries::filterForDeleteableObjects(*project, {luaSource}).empty());

		cmd->deleteObjects({luaSink});

		EXPECT_FALSE(Queries::filterForDeleteableObjects(*project, {luaSource}).empty());
	});
}

TEST_F(ExtrefTest, extref_cant_move) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto mesh = create<Mesh>("mesh");
		auto material = create<Material>("material");
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create<MeshNode>("meshnode", prefab);
		cmd->set({meshnode, {"mesh"}}, mesh);
	});

	setupComposite(basePathName, compositePathName, {"prefab", "material"}, [this]() {
		auto node = create<Node>("local_node");

		auto prefab = findExt("prefab");
		auto material = findExt("material");
		auto mesh = findExt("mesh");
		auto meshnode = findExt("meshnode");

		EXPECT_TRUE(Queries::filterForMoveableScenegraphChildren(*project, {meshnode}, node).empty());
		EXPECT_TRUE(Queries::filterForMoveableScenegraphChildren(*project, {meshnode}, nullptr).empty());
		EXPECT_TRUE(Queries::filterForMoveableScenegraphChildren(*project, {node}, meshnode).empty());
	});
}

TEST_F(ExtrefTest, extref_cant_paste_into) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto material = create<Material>("material");
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create<MeshNode>("meshnode", prefab);
	});

	setupComposite(basePathName, compositePathName, {"prefab", "material"}, [this]() {
		auto node = create<Node>("local_node");

		auto prefab = findExt("prefab");
		auto material = findExt("material");
		auto meshnode = findExt("meshnode");

		EXPECT_FALSE(Queries::canPasteIntoObject(*project, prefab));
		EXPECT_FALSE(Queries::canPasteIntoObject(*project, meshnode));
		EXPECT_FALSE(Queries::canPasteIntoObject(*project, material));
		EXPECT_TRUE(Queries::canPasteIntoObject(*project, node));
	});
}

TEST_F(ExtrefTest, prefab_update_create_child) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this](){
		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_child", prefab);
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this](){
		auto prefab = findExt("prefab");
		auto node = findExt("prefab_child");
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node}));
	});
	
	updateBase(basePathName, [this]() {
		auto prefab = find("prefab");
		auto meshnode = create<MeshNode>("prefab_child_meshnode", prefab);
	});

	updateComposite(compositePathName, [this]() {
		auto prefab = findExt("prefab");
		auto node = findExt("prefab_child");
		auto meshnode = findExt("prefab_child_meshnode");
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node, meshnode}));
	});
}

TEST_F(ExtrefTest, prefab_update_delete_child) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_child", prefab);
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
		auto prefab = findExt("prefab");
		auto node = findExt("prefab_child");
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node}));
	});

	updateBase(basePathName, [this]() {
		auto node = find("prefab_child");
		cmd->deleteObjects({node});
	});

	updateComposite(compositePathName, [this]() {
		auto prefab = findExt("prefab");
		dontFind("prefab_child");
	});
}


TEST_F(ExtrefTest, prefab_update_stop_using_child) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto left = create<Prefab>("prefab_left");
		auto right = create<Prefab>("prefab_right");
		auto node = create<Node>("node", left);
	});

	setupComposite(basePathName, compositePathName, {"prefab_left"}, [this]() {
		auto left = findExt("prefab_left");
		auto node = findExt("node");
		ASSERT_EQ(left->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node}));
	});

	updateBase(basePathName, [this]() {
		auto right = find("prefab_right");
		auto node = find("node");
		cmd->moveScenegraphChildren({node}, right);
	});

	updateComposite(compositePathName, [this]() {
		auto left = findExt("prefab_left");
		ASSERT_EQ(left->children_->size(), 0);
		dontFind("node");
	});
}
TEST_F(ExtrefTest, prefab_update_inter_prefab_scenegraph_move) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto left = create<Prefab>("prefab_left");
		auto right = create<Prefab>("prefab_right");
		auto node = create<Node>("node", left);
	});

	setupComposite(basePathName, compositePathName, {"prefab_left", "prefab_right"}, [this]() {
		auto left = findExt("prefab_left");
		auto right = findExt("prefab_right");
		auto node = findExt("node");
		ASSERT_EQ(left->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node}));
		ASSERT_EQ(right->children_->size(), 0);
	});

	updateBase(basePathName, [this]() {
		auto right = find("prefab_right");
		auto node = find("node");
		cmd->moveScenegraphChildren({node}, right);
	});

	updateComposite(compositePathName, [this]() {
		auto left = findExt("prefab_left");
		auto right = findExt("prefab_right");
		auto node = findExt("node");

		ASSERT_EQ(left->children_->size(), 0);
		ASSERT_EQ(right->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node}));
	});
}

TEST_F(ExtrefTest, prefab_update_move_and_delete_parent) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_node", prefab);
		auto child = create<Node>("prefab_child", node);
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
		auto prefab = find("prefab");
		auto node = find("prefab_node");
		auto child = find("prefab_child");
		EXPECT_EQ(child->getParent(), node);
	});

	updateBase(basePathName, [this]() {
		auto prefab = find("prefab");
		auto node = find("prefab_node");
		auto child = find("prefab_child");
		cmd->moveScenegraphChildren({child}, prefab);
		cmd->deleteObjects({node});
	});

	updateComposite(compositePathName, [this]() {
		auto prefab = find("prefab");
		dontFind("prefab_node");
		auto child = find("prefab_child");
		EXPECT_EQ(child->getParent(), prefab);
	});
}

TEST_F(ExtrefTest, prefab_update_move_and_delete_parent_linked) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_node", prefab);
		auto child = create<Node>("prefab_child", node);
		auto lua = create<LuaScript>("prefab_lua", prefab);

		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.v = VEC3F
	OUT.v = VEC3F
end
function run()
end
)");
		cmd->set({lua, {"uri"}}, scriptFile);
		cmd->addLink({lua, {"luaOutputs", "v"}}, {child, {"translation"}});
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
		auto prefab = find("prefab");
		auto node = find("prefab_node");
		auto child = find("prefab_child");
		auto lua = find("prefab_lua");
		EXPECT_EQ(child->getParent(), node);
		checkLinks({{{lua, {"luaOutputs", "v"}}, {child, {"translation"}}}});
	});

	updateBase(basePathName, [this]() {
		auto prefab = find("prefab");
		auto node = find("prefab_node");
		auto child = find("prefab_child");
		auto lua = find("prefab_lua");
		cmd->moveScenegraphChildren({child}, prefab);
		cmd->deleteObjects({node});
	});

	updateComposite(compositePathName, [this]() {
		auto prefab = find("prefab");
		dontFind("prefab_node");
		auto child = find("prefab_child");
		auto lua = find("prefab_lua");
		EXPECT_EQ(child->getParent(), prefab);
		checkLinks({{{lua, {"luaOutputs", "v"}}, {child, {"translation"}}}});
	});
}
TEST_F(ExtrefTest, update_losing_uniforms) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto mesh = create_mesh("mesh", "meshes/Duck.glb");
		auto material = create_material("material", "shaders/basic.vert", "shaders/basic.frag");
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create_meshnode("prefab_meshnode", mesh, material, prefab);
		cmd->set(meshnode->getMaterialPrivateHandle(0), true);

		ValueHandle matUniforms{material, {"uniforms"}};
		EXPECT_TRUE(matUniforms.hasProperty("u_color"));

		ValueHandle meshnodeUniforms{meshnode, {"materials", "material", "uniforms"}};
		EXPECT_TRUE(meshnodeUniforms.hasProperty("u_color"));
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
		auto mesh = findExt<Mesh>("mesh");
		auto material = findExt<Material>("material");
		auto meshnode = findExt("prefab_meshnode");

		auto local_meshnode = create_meshnode("local_meshnode", mesh, material);
		cmd->set(local_meshnode->getMaterialPrivateHandle(0), true);

		auto ext_mn_uniforms = ValueHandle(meshnode, {"materials"})[0].get("uniforms");
		ASSERT_TRUE(ext_mn_uniforms);
		ASSERT_TRUE(ext_mn_uniforms.hasProperty("u_color"));

		auto local_mn_uniforms = ValueHandle(local_meshnode, {"materials"})[0].get("uniforms");
		ASSERT_TRUE(local_mn_uniforms);
		ASSERT_TRUE(local_mn_uniforms.hasProperty("u_color"));
	});

	updateBase(basePathName, [this]() {
		auto material = find("material");
		cmd->set({material, {"uriVertex"}}, (test_path() / "shaders/nosuchfile.vert").string());
	});

	updateComposite(compositePathName, [this]() {
		auto mesh = findExt<Mesh>("mesh");
		auto material = findExt<Material>("material");
		auto meshnode = findExt("prefab_meshnode");

		auto local_meshnode = find("local_meshnode");

		auto ext_mn_uniforms = ValueHandle(meshnode, {"materials"})[0].get("uniforms");
		ASSERT_TRUE(ext_mn_uniforms);
		ASSERT_FALSE(ext_mn_uniforms.hasProperty("u_color"));

		auto local_mn_uniforms = ValueHandle(local_meshnode, {"materials"})[0].get("uniforms");
		ASSERT_TRUE(local_mn_uniforms);
		ASSERT_FALSE(local_mn_uniforms.hasProperty("u_color"));
	});
}

TEST_F(ExtrefTest, prefab_instance_update) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_child", prefab);
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
		auto prefab = findExt("prefab");
		auto node = findExt("prefab_child");
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node}));

		auto inst = create<PrefabInstance>("inst");
		cmd->set({inst, {"template"}}, prefab);

		auto inst_children = inst->children_->asVector<SEditorObject>();
		ASSERT_EQ(inst_children.size(), 1);
		auto inst_node = inst_children[0]->as<Node>();
		ASSERT_TRUE(inst_node != nullptr);
		ASSERT_TRUE(inst_node->query<ExternalReferenceAnnotation>() == nullptr);
	});

	updateBase(basePathName, [this]() {
		auto prefab = find("prefab");
		auto meshnode = create<MeshNode>("prefab_child_meshnode", prefab);
	});

	updateComposite(compositePathName, [this]() {
		auto prefab = findExt("prefab");
		auto node = findExt("prefab_child");
		auto inst = find("inst");

		auto meshnode = findExt("prefab_child_meshnode");
		ASSERT_TRUE(meshnode != nullptr);
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node, meshnode}));

		auto inst_children = inst->children_->asVector<SEditorObject>();
		ASSERT_EQ(inst_children.size(), 2);
		for (auto child : inst_children) {
			ASSERT_TRUE(child->query<ExternalReferenceAnnotation>() == nullptr);
		}
	});
}

// Temporarily disable test: implementation of the feature was badly broken and the feature was effectively disabled.
//TEST_F(ExtrefTest, prefab_instance_update_lua_new_property) {
//	auto basePathName{(test_path() / "base.rcp").string()};
//	auto compositePathName{(test_path() / "composite.rcp").string()};
//
//	TextFile scriptFile = makeFile("script.lua", R"(
//function interface()
//	IN.i = INT
//end
//function run()
//end
//)");
//
//	setupBase(basePathName, [this, &scriptFile]() {
//		auto prefab = create<Prefab>("prefab");
//		auto lua = create<LuaScript>("prefab_lua", prefab);
//		cmd->set({lua, {"uri"}}, scriptFile);
//		cmd->set({lua, {"luaInputs", "i"}}, 7);
//	});
//
//	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
//		auto prefab = findExt("prefab");
//		auto lua = findExt<LuaScript>("prefab_lua");
//		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({lua}));
//
//		auto inst = create<PrefabInstance>("inst");
//		cmd->set({inst, {"template"}}, prefab);
//		auto inst_children = inst->children_->asVector<SEditorObject>();
//		ASSERT_EQ(inst_children.size(), 1);
//		auto inst_lua = inst_children[0]->as<LuaScript>();
//		ASSERT_EQ(lua->luaInputs_->get("i")->asInt(), inst_lua->luaInputs_->get("i")->asInt());
//
//		cmd->set({inst_lua, {"luaInputs", "i"}}, 11);
//	});
//
//	raco::utils::file::write(scriptFile.path.string(), R"(
//function interface()
//	IN.i = INT
//	IN.f = FLOAT
//end
//function run()
//end
//)");
//
//	updateBase(basePathName, [this]() {
//		auto lua = find("prefab_lua");
//		cmd->set({lua, {"luaInputs", "f"}}, 13.0);
//	});
//
//	updateComposite(compositePathName, [this]() {
//		auto inst = find("inst");
//		auto inst_lua = inst->children_->get(0)->asRef()->as<LuaScript>();
//		ASSERT_EQ(inst_lua->luaInputs_->get("i")->asInt(), 11);
//		ASSERT_EQ(inst_lua->luaInputs_->get("f")->asDouble(), 13.0);
//	});
//}

TEST_F(ExtrefTest, prefab_instance_lua_update_link) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_child", prefab);
		auto lua = create<LuaScript>("prefab_lua", prefab);

		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.v = VEC3F
	OUT.v = VEC3F
end
function run()
end
)");
		cmd->set({lua, {"uri"}}, scriptFile);
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
		auto prefab = find("prefab");
		auto node = find("prefab_child");
		auto lua = find("prefab_lua");
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node, lua}));
	});

	updateBase(basePathName, [this]() {
		auto node = find("prefab_child");
		auto lua = find("prefab_lua");
		cmd->addLink({lua, {"luaOutputs", "v"}}, {node, {"translation"}});
	});

	updateComposite(compositePathName, [this]() {
		auto prefab = find("prefab");
		auto node = find("prefab_child");
		auto lua = find("prefab_lua");
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({node, lua}));

		auto link = Queries::getLink(*project, {node, {"translation"}});
		EXPECT_TRUE(link && link->startProp() == PropertyDescriptor(lua, {"luaOutputs", "v"}));
	});
}

TEST_F(ExtrefTest, duplicate_link_paste_prefab) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_child", prefab);
		auto lua = create<LuaScript>("prefab_lua", prefab);

		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.v = VEC3F
	OUT.v = VEC3F
end
function run()
end
)");
		cmd->set({lua, {"uri"}}, scriptFile);
		cmd->addLink({lua, {"luaOutputs", "v"}}, {node, {"translation"}});
	});

	setupGeneric([this, basePathName]() {
		ASSERT_TRUE(pasteFromExt(basePathName, {"prefab"}, true));
		ASSERT_EQ(project->links().size(), 1);
		ASSERT_TRUE(pasteFromExt(basePathName, {"prefab"}, true));
		ASSERT_EQ(project->links().size(), 1);
	});
}

TEST_F(ExtrefTest, duplicate_link_paste_lua) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto start = create<LuaScript>("start");
		auto end = create<LuaScript>("end");

		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.v = VEC3F
	OUT.v = VEC3F
end
function run()
end
)");
		cmd->set({start, {"uri"}}, scriptFile);
		cmd->set({end, {"uri"}}, scriptFile);
		cmd->addLink({start, {"luaOutputs", "v"}}, {end, {"luaInputs", "v"}});
	});

	setupGeneric([this, basePathName]() {
		ASSERT_TRUE(pasteFromExt(basePathName, {"end"}, true));
		auto start = findExt("start");
		auto end = findExt("end");
		checkLinks({{{start, {"luaOutputs", "v"}}, {end, {"luaInputs", "v"}}}});
		ASSERT_TRUE(pasteFromExt(basePathName, {"end"}, true));
		checkLinks({{{start, {"luaOutputs", "v"}}, {end, {"luaInputs", "v"}}}});
	});
}

TEST_F(ExtrefTest, duplicate_link_paste_chained_lua) {
	auto basePathName{(test_path() / "base.rcp").string()};

	setupBase(basePathName, [this]() {
		auto start = create<LuaScript>("start");
		auto mid = create<LuaScript>("mid");
		auto end = create<LuaScript>("end");

		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.v = VEC3F
	OUT.v = VEC3F
end
function run()
end
)");
		cmd->set({start, {"uri"}}, scriptFile);
		cmd->set({mid, {"uri"}}, scriptFile);
		cmd->set({end, {"uri"}}, scriptFile);
		cmd->addLink({start, {"luaOutputs", "v"}}, {mid, {"luaInputs", "v"}});
		cmd->addLink({mid, {"luaOutputs", "v"}}, {end, {"luaInputs", "v"}});
	});

	setupGeneric([this, basePathName]() {
		ASSERT_TRUE(pasteFromExt(basePathName, {"mid"}, true));
		auto start = findExt("start");
		auto mid = findExt("mid");
		checkLinks({{{start, {"luaOutputs", "v"}}, {mid, {"luaInputs", "v"}}}});
		ASSERT_TRUE(pasteFromExt(basePathName, {"mid", "end"}, true));
		auto end = findExt("end");
		checkLinks({
			{{start, {"luaOutputs", "v"}}, {mid, {"luaInputs", "v"}}},
			{{mid, {"luaOutputs", "v"}}, {end, {"luaInputs", "v"}}}
		});
	});
}

TEST_F(ExtrefTest, nesting_create) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto midPathName((test_path() / "mid.rcp").string());
	auto compositePathName{(test_path() / "composite.rcp").string()};

	std::string base_id;
	std::string mid_id;

	setupBase(basePathName, [this, &base_id]() {
		//auto mesh = create_mesh("mesh", "meshes/Duck.glb");
		auto mesh = create<Mesh>("mesh");
		base_id = project->projectID();
	});

	setupComposite(basePathName, midPathName, {"mesh"}, [this, &mid_id]() {
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create<MeshNode>("prefab_child", prefab);
		auto mesh = findExt("mesh");
		cmd->set({meshnode, {"mesh"}}, mesh);
		mid_id = project->projectID();
	}, "mid");

	setupComposite(midPathName, compositePathName, {"prefab"}, [this, base_id, mid_id]() {
		auto prefab = findExt("prefab", mid_id);
		auto meshnode = findExt<MeshNode>("prefab_child", mid_id);
		auto mesh = findExt("mesh", base_id);
		ASSERT_EQ(prefab->children_->asVector<SEditorObject>(), std::vector<SEditorObject>({meshnode}));
		ASSERT_EQ(*meshnode->mesh_, mesh);
	});
}

TEST_F(ExtrefTest, filecopy_update_fail_nested_same_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};
	auto midPathName((test_path() / "mid.rcp").string());
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto mesh = create<Mesh>("mesh");
	}, std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		rename_project("copy");
	});

	setupBase(midPathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create<MeshNode>("prefab_child", prefab);
	}, "mid");

	setupBase(
		compositePathName, [this, basePathName1, midPathName]() {
			ASSERT_TRUE(pasteFromExt(basePathName1, {"mesh"}, true));
			ASSERT_TRUE(pasteFromExt(midPathName, {"prefab"}, true));
		},
		"composite");

	updateBase(midPathName, [this, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName2, {"mesh"}, true));

		auto meshnode = find("prefab_child");
		auto mesh = findExt("mesh");
		cmd->set({meshnode, {"mesh"}}, mesh);
	});

	EXPECT_THROW(updateComposite(compositePathName, [this]() {}), raco::core::ExtrefError);
}

TEST_F(ExtrefTest, filecopy_update_fail_nested_different_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};
	auto midPathName((test_path() / "mid.rcp").string());
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto mesh = create<Mesh>("mesh");
		auto material = create<Material>("material");
	}, std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		rename_project("copy");
	});

	setupBase(midPathName, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create<MeshNode>("prefab_child", prefab);
	}, "mid");

	setupBase(
		compositePathName, [this, basePathName1, midPathName]() {
			ASSERT_TRUE(pasteFromExt(basePathName1, {"material"}, true));
			ASSERT_TRUE(pasteFromExt(midPathName, {"prefab"}, true));
		},
		"composite");

	updateBase(midPathName, [this, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName2, {"mesh"}, true));

		auto meshnode = find("prefab_child");
		auto mesh = findExt("mesh");
		cmd->set({meshnode, {"mesh"}}, mesh);
	});

	EXPECT_THROW(updateComposite(compositePathName, [this]() {}), raco::core::ExtrefError);
}


TEST_F(ExtrefTest, nesting_create_loop_fail) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto prefab = create<Prefab>("prefab");
	});

	setupComposite(basePathName, compositePathName, {"prefab"}, [this]() {
		auto prefab_base= findExt("prefab");
		auto prefab_comp = create<Prefab>("prefab_comp");
	}, "composite");

	updateComposite(basePathName, [this, compositePathName]() {
		ASSERT_FALSE(pasteFromExt(compositePathName, {"prefab_comp"}, true));
	});
}

TEST_F(ExtrefTest, nested_shared_material_update) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto duckPathName((test_path() / "duck.rcp").string());
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto material = create_material("material", "shaders/basic.vert", "shaders/basic.frag");
	});

	setupComposite(
		basePathName, duckPathName, {"material"}, [this]() {
			auto prefab = create<Prefab>("prefab_duck");
			auto mesh = create_mesh("mesh", "meshes/Duck.glb");
			auto material = findExt<Material>("material");
			auto meshnode = create_meshnode("meshnode", mesh, material, prefab);
		},
		"duck");

	setupComposite(duckPathName, compositePathName, {"prefab_duck"}, [this]() {
	});

	updateBase(basePathName, [this]() {
		auto material = find("material");
		cmd->set({material, {"uniforms", "u_color", "x"}}, 0.5);
	});

	updateComposite(compositePathName, [this]() {
		auto material = findExt<Material>("material");
		ASSERT_EQ(ValueHandle(material, {"uniforms", "u_color", "x"}).asDouble(), 0.5);
	});

}

TEST_F(ExtrefTest, meshnode_uniform_refs_private_material) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto mesh = create_mesh("mesh", "meshes/Duck.glb");
		auto material = create_material("material", "shaders/simple_texture.vert", "shaders/simple_texture.frag");
		auto texture = create<Texture>("texture");
		auto prefab = create<Prefab>("prefab_duck");
		auto meshnode = create_meshnode("meshnode", mesh, material, prefab);
		cmd->set(meshnode->getMaterialPrivateHandle(0), true);
		cmd->set({meshnode, {"materials", "material", "uniforms", "u_Tex"}}, texture);
	});

	setupComposite(basePathName, compositePathName, {"prefab_duck"}, [this]() {
		auto texture = findExt<Texture>("texture");
		auto material = findExt<Material>("material");
		auto mesh = findExt<Mesh>("mesh");
	});
}

TEST_F(ExtrefTest, meshnode_uniform_refs_shared_material) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto mesh = create_mesh("mesh", "meshes/Duck.glb");
		auto material = create_material("material", "shaders/simple_texture.vert", "shaders/simple_texture.frag");
		auto texture = create<Texture>("texture");
		auto prefab = create<Prefab>("prefab_duck");
		auto meshnode = create_meshnode("meshnode", mesh, material, prefab);
		ASSERT_FALSE(meshnode->materialPrivate(0));
		ASSERT_EQ(meshnode->getUniformContainer(0)->size(), 0);

		cmd->set(meshnode->getMaterialPrivateHandle(0), true);
		ASSERT_EQ(meshnode->getUniformContainer(0)->size(), 1);

		cmd->set({meshnode, {"materials", "material", "uniforms", "u_Tex"}}, texture);
		cmd->set(meshnode->getMaterialPrivateHandle(0), false);
		ASSERT_EQ(meshnode->getUniformContainer(0)->size(), 0);
	});

	setupComposite(basePathName, compositePathName, {"prefab_duck"}, [this]() {
		dontFind("texture");
		auto material = findExt<Material>("material");
		auto mesh = findExt<Mesh>("mesh");
	});
}


TEST_F(ExtrefTest, nested_shared_material_linked_update) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto duckPathName((test_path() / "duck.rcp").string());
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto material = create_material("material", "shaders/basic.vert", "shaders/basic.frag");
		auto lua = create<LuaScript>("global_control");

		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.scalar = FLOAT
	OUT.color = VEC3F
end
function run()
	OUT.color = {IN.scalar, IN.scalar, 0.0}
end
)");
		cmd->set({lua, {"uri"}}, scriptFile);
		cmd->addLink({lua, {"luaOutputs", "color"}}, {material, {"uniforms", "u_color"}});
	});

	setupComposite(
		basePathName, duckPathName, {"material"}, [this]() {
			auto prefab = create<Prefab>("prefab_duck");
			auto mesh = create_mesh("mesh", "meshes/Duck.glb");
			auto material = findExt<Material>("material");
			auto meshnode = create_meshnode("meshnode", mesh, material, prefab);
		},
		"duck");

	setupComposite(duckPathName, compositePathName, {"prefab_duck"}, [this]() {
		auto material = findExt<Material>("material");
		ASSERT_EQ(ValueHandle(material, {"uniforms", "u_color", "x"}).asDouble(), 0.0);
	});

	updateBase(basePathName, [this]() {
		auto lua = find("global_control");
		cmd->set({lua, {"luaInputs", "scalar"}}, 0.5);
	});

	updateComposite(compositePathName, [this]() {
		app->doOneLoop();
		auto material = findExt<Material>("material");
		ASSERT_EQ(ValueHandle(material, {"uniforms", "u_color", "x"}).asDouble(), 0.5);
	});
}

TEST_F(ExtrefTest, shared_material_stacked_lua_linked_update) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto material = create_material("material", "shaders/basic.vert", "shaders/basic.frag");
		auto lua = create<LuaScript>("global_control");
		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.scalar = FLOAT
	OUT.color = VEC3F
end
function run()
	OUT.color = {IN.scalar, IN.scalar, 0.0}
end
)");
		cmd->set({lua, {"uri"}}, scriptFile);
		cmd->addLink({lua, {"luaOutputs", "color"}}, {material, {"uniforms", "u_color"}});

		auto master = create<LuaScript>("master_control");
		TextFile masterScriptFile = makeFile("master.lua", R"(
function interface()
	IN.scalar = FLOAT
	OUT.mat = FLOAT
end
function run()
	OUT.mat = 3 * IN.scalar;
end
)");
		cmd->set({master, {"uri"}}, masterScriptFile);
		cmd->addLink({master, {"luaOutputs", "mat"}}, {lua, {"luaInputs", "scalar"}});
	});

	setupComposite(basePathName, compositePathName, {"material"}, [this]() {
		auto material = findExt<Material>("material");
		ASSERT_EQ(ValueHandle(material, {"uniforms", "u_color", "x"}).asDouble(), 0.0);
	});

	updateBase(basePathName, [this]() {
		auto lua = find("master_control");
		cmd->set({lua, {"luaInputs", "scalar"}}, 0.5);
	});

	updateComposite(compositePathName, [this]() {
		app->doOneLoop();
		auto material = findExt<Material>("material");
		ASSERT_EQ(ValueHandle(material, {"uniforms", "u_color", "x"}).asDouble(), 1.5);
	});
}

TEST_F(ExtrefTest, diamond_shared_material_linked_update) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto duckPathName((test_path() / "duck.rcp").string());
	auto quadPathName((test_path() / "quad.rcp").string());
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto material = create_material("material", "shaders/basic.vert", "shaders/basic.frag");
		auto lua = create<LuaScript>("global_control");

		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.scalar = FLOAT
	OUT.color = VEC3F
end
function run()
	OUT.color = {IN.scalar, IN.scalar, 0.0}
end
)");
		cmd->set({lua, {"uri"}}, scriptFile);
		cmd->addLink({lua, {"luaOutputs", "color"}}, {material, {"uniforms", "u_color"}});
	});

	setupComposite(
		basePathName, duckPathName, {"material"}, [this]() {
			auto prefab = create<Prefab>("prefab_duck");
			auto mesh = create_mesh("mesh", "meshes/Duck.glb");
			auto material = findExt<Material>("material");
			auto meshnode = create_meshnode("meshnode", mesh, material, prefab);

			ASSERT_EQ(countInstances<Material>(), 1);
		},
		"duck");

	setupComposite(
		basePathName, quadPathName, {"material"}, [this]() {
			auto prefab = create<Prefab>("prefab_quad");
			auto mesh = create_mesh("mesh", "meshes/defaultQuad.gltf");
			auto material = findExt<Material>("material");
			auto meshnode = create_meshnode("meshnode", mesh, material, prefab);

			ASSERT_EQ(countInstances<Material>(), 1);
		},
		"quad");

	setupBase(compositePathName, [this, duckPathName, quadPathName]() {
		pasteFromExt(duckPathName, {"prefab_duck"}, true);
		pasteFromExt(quadPathName, {"prefab_quad"}, true);

		ASSERT_EQ(countInstances<LuaScript>(), 1);
		ASSERT_EQ(countInstances<Material>(), 1);

		auto material = findExt<Material>("material");
		ASSERT_EQ(ValueHandle(material, {"uniforms", "u_color", "x"}).asDouble(), 0.0);
	});

	updateBase(basePathName, [this]() {
		auto lua = find("global_control");
		cmd->set({lua, {"luaInputs", "scalar"}}, 0.5);
	});

	updateComposite(compositePathName, [this]() {
		app->doOneLoop();
		auto material = findExt<Material>("material");
		ASSERT_EQ(ValueHandle(material, {"uniforms", "u_color", "x"}).asDouble(), 0.5);
	});
}

TEST_F(ExtrefTest, diamond_shared_material_linked_move_lua) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto duckPathName((test_path() / "duck.rcp").string());
	auto quadPathName((test_path() / "quad.rcp").string());
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {
		auto material = create_material("material", "shaders/basic.vert", "shaders/basic.frag");
		auto lua = create<LuaScript>("global_control");

		TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.scalar = FLOAT
	OUT.color = VEC3F
end
function run()
	OUT.color = {IN.scalar, IN.scalar, 0.0}
end
)");
		cmd->set({lua, {"uri"}}, scriptFile);
		cmd->addLink({lua, {"luaOutputs", "color"}}, {material, {"uniforms", "u_color"}});
	});

	setupComposite(
		basePathName, duckPathName, {"material"}, [this]() {
			auto prefab = create<Prefab>("prefab_duck");
			auto mesh = create_mesh("mesh", "meshes/Duck.glb");
			auto material = findExt<Material>("material");
			auto meshnode = create_meshnode("meshnode", mesh, material, prefab);

			ASSERT_EQ(countInstances<Material>(), 1);
		},
		"duck");

	setupComposite(
		basePathName, quadPathName, {"material"}, [this]() {
			auto prefab = create<Prefab>("prefab_quad");
			auto mesh = create_mesh("mesh", "meshes/defaultQuad.gltf");
			auto material = findExt<Material>("material");
			auto meshnode = create_meshnode("meshnode", mesh, material, prefab);

			ASSERT_EQ(countInstances<Material>(), 1);
		},
		"quad");

	setupBase(compositePathName, [this, duckPathName, quadPathName]() {
		pasteFromExt(duckPathName, {"prefab_duck"}, true);
		pasteFromExt(quadPathName, {"prefab_quad"}, true);

		ASSERT_EQ(countInstances<LuaScript>(), 1);
		ASSERT_EQ(countInstances<Material>(), 1);

		auto material = findExt<Material>("material");
		ASSERT_EQ(ValueHandle(material, {"uniforms", "u_color", "x"}).asDouble(), 0.0);
	});

	updateBase(basePathName, [this]() {
		auto node = create<Node>("dummyNode");
		auto lua = find("global_control");
		cmd->moveScenegraphChildren({lua}, node);
		cmd->set({lua, {"luaInputs", "scalar"}}, 0.5);
	});

	updateComposite(compositePathName, [this]() {
		app->doOneLoop();
		ASSERT_EQ(countInstances<LuaScript>(), 0);
		ASSERT_EQ(countInstances<Material>(), 1);
		auto material = findExt<Material>("material");
		ASSERT_EQ(ValueHandle(material, {"uniforms", "u_color", "x"}).asDouble(), 0.0);
	});
}

TEST_F(ExtrefTest, saveas_reroot_uri_lua) {
	auto basePathName{(test_path() / "base.rcp").string()};

	auto base_id = setupBase(basePathName, [this, basePathName]() {
		project->setCurrentPath(basePathName);

		auto prefab = create<Prefab>("prefab");
		auto lua = create<LuaScript>("lua", prefab);
		cmd->set({lua, {"uri"}}, std::string("relativeURI"));
	});
	
	std::filesystem::create_directory((test_path() / "subdir"));
	setupGeneric([this, basePathName, base_id]() {
		project->setCurrentPath((test_path() / "project.file").string());

		pasteFromExt(basePathName, {"prefab"}, true);
		auto prefab = findExt("prefab");
		auto lua_prefab = findExt<LuaScript>("lua");

		auto inst = create<PrefabInstance>("inst");
		cmd->set({inst, {"template"}}, prefab);

		auto inst_children = inst->children_->asVector<SEditorObject>();
		ASSERT_EQ(inst_children.size(), 1);
		auto lua_inst = inst_children[0]->as<LuaScript>();
		ASSERT_TRUE(lua_inst != nullptr);

		auto lua_local = create<LuaScript>("lua_local");
		cmd->set({lua_local, {"uri"}}, std::string("relativeURI"));

		EXPECT_EQ(*lua_prefab->uri_, std::string("relativeURI"));
		EXPECT_EQ(*lua_inst->uri_, std::string("relativeURI"));
		EXPECT_EQ(*lua_local->uri_, std::string("relativeURI"));

		ASSERT_EQ(project->externalProjectsMap().at(base_id).path, "base.rcp");

		ASSERT_TRUE(app->activeRaCoProject().saveAs((test_path() / "subdir" / "project.file").string().c_str()));

		EXPECT_EQ(*lua_prefab->uri_, std::string("relativeURI"));
		EXPECT_EQ(*lua_inst->uri_, std::string("relativeURI"));
		EXPECT_EQ(*lua_local->uri_, std::string("../relativeURI"));

		ASSERT_EQ(project->externalProjectsMap().at(base_id).path, "../base.rcp");
	});
}

TEST_F(ExtrefTest, paste_reroot_lua_uri_dir_up) {
	auto basePathName{(test_path() / "base.rcp").string()};

	auto base_id = setupBase(basePathName, [this, basePathName]() {
		project->setCurrentPath(basePathName);

		auto prefab = create<Prefab>("prefab");
		auto lua = create<LuaScript>("lua", prefab);
		cmd->set({lua, {"uri"}}, std::string("relativeURI"));
	});

	setupGeneric([this, basePathName, base_id]() {
		project->setCurrentPath((test_path() / "subdir" / "project.file").string());

		pasteFromExt(basePathName, {"prefab"}, true);
		auto prefab = findExt("prefab");
		auto lua_prefab = findExt<LuaScript>("lua");

		EXPECT_EQ(*lua_prefab->uri_, std::string("relativeURI"));
		ASSERT_EQ(project->externalProjectsMap().at(base_id).path, "../base.rcp");

		auto pasted = cmd->pasteObjects(cmd->copyObjects({lua_prefab}));
		EXPECT_EQ(*pasted[0]->as<LuaScript>()->uri_, std::string("../relativeURI"));

		auto inst = create<PrefabInstance>("inst");
		cmd->set({inst, {"template"}}, prefab);
		auto lua_inst = inst->children_->asVector<SEditorObject>()[0]->as<LuaScript>();
		EXPECT_EQ(*lua_inst->uri_, std::string("relativeURI"));
		
		auto pasted_lua_inst = cmd->pasteObjects(cmd->copyObjects({lua_inst}));
		EXPECT_EQ(*pasted_lua_inst[0]->as<LuaScript>()->uri_, std::string("../relativeURI"));

		auto pasted_inst = cmd->pasteObjects(cmd->copyObjects({inst}));
		EXPECT_EQ(pasted_inst.size(), 1);
		auto pasted_inst_child_lua = pasted_inst[0]->children_->asVector<SEditorObject>()[0]->as<LuaScript>();
		EXPECT_EQ(*pasted_inst_child_lua->uri_, std::string("relativeURI"));
	});
}

TEST_F(ExtrefTest, paste_reroot_lua_uri_dir_down) {
	std::filesystem::create_directory((test_path() / "subdir"));
	auto basePathName{(test_path() / "subdir" / "base.rcp").string()};

	auto base_id = setupBase(basePathName, [this, basePathName]() {
		project->setCurrentPath(basePathName);

		auto prefab = create<Prefab>("prefab");
		auto lua = create<LuaScript>("lua", prefab);
		cmd->set({lua, {"uri"}}, std::string("relativeURI"));
	});

	setupGeneric([this, basePathName, base_id]() {
		project->setCurrentPath((test_path() / "project.file").string());

		pasteFromExt(basePathName, {"prefab"}, true);
		auto prefab = findExt("prefab");
		auto lua_prefab = findExt<LuaScript>("lua");

		EXPECT_EQ(*lua_prefab->uri_, std::string("relativeURI"));
		ASSERT_EQ(project->externalProjectsMap().at(base_id).path, "subdir/base.rcp");

		auto pasted = cmd->pasteObjects(cmd->copyObjects({lua_prefab}));
		EXPECT_EQ(*pasted[0]->as<LuaScript>()->uri_, std::string("subdir/relativeURI"));

		auto inst = create<PrefabInstance>("inst");
		cmd->set({inst, {"template"}}, prefab);
		auto lua_inst = inst->children_->asVector<SEditorObject>()[0]->as<LuaScript>();
		EXPECT_EQ(*lua_inst->uri_, std::string("relativeURI"));

		auto pasted_lua_inst = cmd->pasteObjects(cmd->copyObjects({lua_inst}));
		EXPECT_EQ(*pasted_lua_inst[0]->as<LuaScript>()->uri_, std::string("subdir/relativeURI"));

		auto pasted_inst = cmd->pasteObjects(cmd->copyObjects({inst}));
		EXPECT_EQ(pasted_inst.size(), 1);
		auto pasted_inst_child_lua = pasted_inst[0]->children_->asVector<SEditorObject>()[0]->as<LuaScript>();
		EXPECT_EQ(*pasted_inst_child_lua->uri_, std::string("relativeURI"));
	});
}

TEST_F(ExtrefTest, paste_reroot_lua_uri_with_link_down) {
	std::filesystem::create_directory((test_path() / "subdir"));
	auto basePathName{(test_path() / "subdir" / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this, basePathName]() {
		project->setCurrentPath(basePathName);

		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_node", prefab);
		auto lua = create<LuaScript>("prefab_lua", prefab);
		
		raco::utils::file::write((test_path() / "subdir/script.lua").string(), R"(
function interface()
	IN.v = VEC3F
	OUT.v = VEC3F
end
function run()
end
)");
		cmd->set({lua, {"uri"}}, std::string("script.lua"));
		cmd->addLink({lua, {"luaOutputs", "v"}}, {node, {"translation"}});
	});

	setupGeneric([this, basePathName]() {
		project->setCurrentPath((test_path() / "project.file").string());

		pasteFromExt(basePathName, {"prefab"}, true);
		auto prefab = findExt("prefab");
		auto prefab_lua = findExt<LuaScript>("prefab_lua");
		auto prefab_node = findExt<Node>("prefab_node");

		EXPECT_EQ(*prefab_lua->uri_, std::string("script.lua"));
		checkLinks({{{prefab_lua, {"luaOutputs", "v"}}, {prefab_node, {"translation"}}}});

		auto inst = create<PrefabInstance>("inst");
		cmd->set({inst, {"template"}}, prefab);
		auto inst_lua = findChild<LuaScript>(inst);
		auto inst_node = findChild<Node>(inst);
		EXPECT_EQ(*inst_lua->uri_, std::string("script.lua"));
		checkLinks({
			{{prefab_lua, {"luaOutputs", "v"}}, {prefab_node, {"translation"}}},
			{{inst_lua, {"luaOutputs", "v"}}, {inst_node, {"translation"}}}});

		auto pasted_inst = cmd->pasteObjects(cmd->copyObjects({inst}));
		EXPECT_EQ(pasted_inst.size(), 1);
		auto pasted_inst_lua = findChild<LuaScript>(pasted_inst[0]);
		auto pasted_inst_node = findChild<Node>(pasted_inst[0]);
		EXPECT_EQ(*pasted_inst_lua->uri_, std::string("script.lua"));
		checkLinks({{{prefab_lua, {"luaOutputs", "v"}}, {prefab_node, {"translation"}}},
			{{inst_lua, {"luaOutputs", "v"}}, {inst_node, {"translation"}}},
			{{pasted_inst_lua, {"luaOutputs", "v"}}, {pasted_inst_node, {"translation"}}}});
	});
}

TEST_F(ExtrefTest, prefab_link_quaternion_in_prefab) {
	auto basePathName1{(test_path() / "base.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(basePathName1, [this, basePathName1]() {
		project->setCurrentPath(basePathName1);

		auto prefab = create<Prefab>("prefab");
		auto node = create<Node>("prefab_node", prefab);
		auto lua = create<LuaScript>("prefab_lua", prefab);

		raco::utils::file::write((test_path() / "script.lua").string(), R"(
function interface()
	IN.v = VEC4F
	OUT.v = VEC4F
end
function run()
OUT.v = IN.v
end
)");
		cmd->moveScenegraphChildren({node}, prefab);
		cmd->moveScenegraphChildren({lua}, prefab);
		cmd->set({lua, {"uri"}}, std::string("script.lua"));
		cmd->addLink({lua, {"luaOutputs", "v"}}, {node, {"rotation"}});
	});

	setupBase(basePathName2, [this, basePathName1, basePathName2]() {
		project->setCurrentPath(basePathName2);
		pasteFromExt(basePathName1, {"prefab"}, true);
		auto prefab = findExt("prefab");
		auto prefab_lua = findExt<LuaScript>("prefab_lua");
		auto prefab_node = findExt<Node>("prefab_node");

		auto inst = create<PrefabInstance>("inst");
		cmd->set({inst, {"template"}}, prefab);

		auto inst_children = inst->children_->asVector<SEditorObject>();
		auto inst_node = inst_children[0]->as<Node>();
		auto inst_lua = inst_children[1]->as<LuaScript>();

		checkLinks({{{prefab_lua, {"luaOutputs", "v"}}, {prefab_node, {"rotation"}}},
			{{inst_lua, {"luaOutputs", "v"}}, {inst_node, {"rotation"}}}});
	});
}

TEST_F(ExtrefTest, animation_channel_data_gets_propagated) {
	auto basePathName1{(test_path() / "base.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto animChannel = create_animationchannel("animCh", "meshes/InterpolationTest/InterpolationTest.gltf");
	});

	setupBase(basePathName2, [this, basePathName1]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"animCh"}, true));

		auto anim = create_animation("anim");
		auto animChannel = findExt<AnimationChannel>("animCh");
		cmd->set({anim, {"animationChannels", "Channel 0"}}, animChannel);

		ASSERT_NE(animChannel, nullptr);
		ASSERT_EQ(anim->get("animationChannels")->asTable()[0]->asRef(), animChannel);
	});
}

TEST_F(ExtrefTest, animation_in_extref_prefab_gets_propagated) {
	auto basePathName1{(test_path() / "base.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto anim = create_animation("anim");
		auto animNode = create<Node>("node");
		cmd->moveScenegraphChildren({anim}, animNode);

		auto prefab = create<Prefab>("prefab");
		cmd->moveScenegraphChildren({animNode}, prefab);

		auto animChannel = create_animationchannel("animCh", "meshes/InterpolationTest/InterpolationTest.gltf");
		cmd->set({anim, {"animationChannels", "Channel 0"}}, animChannel);
	});

	setupBase(basePathName2, [this, basePathName1]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"prefab"}, true));

		auto anim = findExt<Animation>("anim");
		auto animChannel = findExt<AnimationChannel>("animCh");

		ASSERT_NE(anim, nullptr);
		ASSERT_NE(animChannel, nullptr);
		ASSERT_EQ(anim->get("animationChannels")->asTable()[0]->asRef(), animChannel);
	});
}

TEST_F(ExtrefTest, prefab_cut_deep_linked_does_not_delete_shared) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(basePathName, [this]() {

		auto mesh1 = create_mesh("mesh1", "meshes/Duck.glb");
		auto mesh2 = create_mesh("mesh2", "meshes/Duck.glb");
		auto sharedMaterial = create_material("sharedMaterial", "shaders/basic.vert", "shaders/basic.frag");

		auto prefab1 = create<Prefab>("prefab1");
		auto prefab2 = create<Prefab>("prefab2");

		auto meshNode1 = create_meshnode("prefab1_meshNode", mesh1, sharedMaterial, prefab1);
		auto meshNode2 = create_meshnode("prefab2_meshNode", mesh2, sharedMaterial, prefab2);

	});

	setupComposite(basePathName, compositePathName, {"prefab1", "prefab2"}, [this]() {
		auto prefab1 = find("prefab1");
		auto prefab2 = find("prefab2");
		
		find("sharedMaterial");

		cmd->cutObjects({prefab2}, true);
		
		find("sharedMaterial");

		cmd->cutObjects({prefab1}, true);
		
		dontFind("sharedMaterial");
	});
}

TEST_F(ExtrefTest, module_gets_propagated) {
	auto basePathName1{(test_path() / "base.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto module = create<LuaScriptModule>("module");
		cmd->set({module, &LuaScriptModule::uri_}, (test_path() / "scripts" / "moduleDefinition.lua").string());
	});

	setupBase(basePathName2, [this, basePathName1]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"module"}, true));
		auto lua = create<LuaScript>("script");
		cmd->set({lua, &LuaScript::uri_}, (test_path() / "scripts" / "moduleDependency.lua").string());

		auto module = findExt<LuaScriptModule>("module");
		cmd->set({lua, {"luaModules", "coalas"}}, module);

		ASSERT_NE(module, nullptr);		
		ASSERT_FALSE(cmd->errors().hasError({lua}));
	});
}

TEST_F(ExtrefTest, prefab_instance_with_lua_and_module) {
	auto basePathName1{(test_path() / "base.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto inst = create<PrefabInstance>("inst");
		cmd->set({inst, &PrefabInstance::template_}, prefab);

		auto lua = create<LuaScript>("lua", prefab);
		cmd->set({lua, &LuaScript::uri_}, (test_path() / "scripts/moduleDependency.lua").string());
		cmd->moveScenegraphChildren({lua}, prefab);

		auto luaModule = create<LuaScriptModule>("luaModule");
		cmd->set({luaModule, &LuaScriptModule::uri_}, (test_path() / "scripts/moduleDefinition.lua").string());

		cmd->set({lua, {"luaModules", "coalas"}}, luaModule);
	});

	setupBase(basePathName2, [this, basePathName1]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"prefab"}, true));
		auto lua = findExt<LuaScript>("lua");
		auto module = findExt<LuaScriptModule>("luaModule");

		ASSERT_NE(lua, nullptr);
		ASSERT_NE(module, nullptr);
		ASSERT_FALSE(cmd->errors().hasError({lua}));
	});
}
TEST_F(ExtrefTest, prefab_instance_update_lua_and_module_remove_module_ref) {
	auto basePathName1{(test_path() / "base.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto inst = create<PrefabInstance>("inst");
		cmd->set({inst, &PrefabInstance::template_}, prefab);

		auto lua = create<LuaScript>("lua", prefab);
		cmd->set({lua, &LuaScript::uri_}, (test_path() / "scripts/moduleDependency.lua").string());
		cmd->moveScenegraphChildren({lua}, prefab);

		auto luaModule = create<LuaScriptModule>("luaModule");
		cmd->set({luaModule, &LuaScriptModule::uri_}, (test_path() / "scripts/moduleDefinition.lua").string());

		cmd->set({lua, {"luaModules", "coalas"}}, luaModule);
	});

	setupBase(basePathName2, [this, basePathName1]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"prefab"}, true));
		auto lua = findExt<LuaScript>("lua");
		auto module = findExt<LuaScriptModule>("luaModule");

		ASSERT_NE(lua, nullptr);
		ASSERT_NE(module, nullptr);
		ASSERT_FALSE(cmd->errors().hasError({lua}));
	});

	updateBase(basePathName1, [this]() {
		auto lua = find("lua");
		cmd->set({lua, {"luaModules", "coalas"}}, SEditorObject{});
	});

	updateBase(basePathName2, [this]() {
		auto lua = findExt<LuaScript>("lua");
		auto module = findExt<LuaScriptModule>("luaModule");

		ASSERT_NE(lua, nullptr);
		ASSERT_NE(module, nullptr);
		ASSERT_TRUE(cmd->errors().hasError({lua}));
	});
}

TEST_F(ExtrefTest, prefab_instance_update_lua_and_module_change_lua_uri) {
	auto basePathName1{(test_path() / "base.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(basePathName1, [this]() {
		auto prefab = create<Prefab>("prefab");
		auto inst = create<PrefabInstance>("inst");
		cmd->set({inst, &PrefabInstance::template_}, prefab);

		auto lua = create<LuaScript>("lua", prefab);
		cmd->set({lua, &LuaScript::uri_}, (test_path() / "scripts/moduleDependency.lua").string());
		cmd->moveScenegraphChildren({lua}, prefab);

		auto luaModule = create<LuaScriptModule>("luaModule");
		cmd->set({luaModule, &LuaScriptModule::uri_}, (test_path() / "scripts/moduleDefinition.lua").string());

		cmd->set({lua, {"luaModules", "coalas"}}, luaModule);
	});

	setupBase(basePathName2, [this, basePathName1]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"prefab"}, true));
		auto lua = findExt<LuaScript>("lua");
		auto module = findExt<LuaScriptModule>("luaModule");

		ASSERT_NE(lua, nullptr);
		ASSERT_NE(module, nullptr);
		ASSERT_FALSE(cmd->errors().hasError({lua}));
	});

	updateBase(basePathName1, [this]() {
		auto lua = find("lua");
		cmd->set({lua, &LuaScript::uri_}, (test_path() / "scripts/types-scalar.lua").string());
	});

	updateBase(basePathName2, [this]() {
		auto lua = findExt<LuaScript>("lua");
		auto module = findExt<LuaScriptModule>("luaModule");

		ASSERT_NE(lua, nullptr);
		ASSERT_NE(module, nullptr);
		ASSERT_FALSE(cmd->errors().hasError({lua}));
	});
}

TEST_F(ExtrefTest, link_extref_to_local_reload) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.v = VEC3F
	OUT.v = VEC3F
end
function run()
end
)");

	setupBase(basePathName, [this, &scriptFile]() {
		auto luaSource = create<LuaScript>("luaSource");

		cmd->set({luaSource, {"uri"}}, scriptFile);
	});

	setupComposite(basePathName, compositePathName, {"luaSource"}, [this, &scriptFile]() {
		auto luaSource = findExt("luaSource");

		auto luaSink = create<LuaScript>("luaSink");
		cmd->set({luaSink, {"uri"}}, scriptFile);
		cmd->addLink({luaSource, {"luaOutputs", "v"}}, {luaSink, {"luaInputs", "v"}});

		EXPECT_EQ(project->links().size(), 1);
	});

	updateComposite(compositePathName, [this]() {
		EXPECT_EQ(project->links().size(), 1);
	});
}

TEST_F(ExtrefTest, link_extref_to_local_update_remove_source) {
	auto basePathName{(test_path() / "base.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	TextFile scriptFile = makeFile("script.lua", R"(
function interface()
	IN.v = VEC3F
	OUT.v = VEC3F
end
function run()
end
)");

	setupBase(basePathName, [this, &scriptFile]() {
		auto luaSource = create<LuaScript>("luaSource");

		cmd->set({luaSource, {"uri"}}, scriptFile);
	});

	setupComposite(basePathName, compositePathName, {"luaSource"}, [this, &scriptFile]() {
		auto luaSource = findExt("luaSource");

		auto luaSink = create<LuaScript>("luaSink");
		cmd->set({luaSink, {"uri"}}, scriptFile);
		cmd->addLink({luaSource, {"luaOutputs", "v"}}, {luaSink, {"luaInputs", "v"}});

		EXPECT_EQ(project->links().size(), 1);
	});

	updateBase(basePathName, [this]() {
		auto luaSource = find("luaSource");
		cmd->deleteObjects({luaSource});
	});

	updateComposite(compositePathName, [this]() {
		EXPECT_EQ(project->links().size(), 0);
	});
}

TEST_F(ExtrefTestWithZips, filecopy_paste_fail_different_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(
		basePathName1, [this]() {
			auto mesh = create<Mesh>("mesh");
			auto prefab = create<Prefab>("prefab");
			auto meshnode = create<MeshNode>("meshnode", prefab);
		},
		std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		rename_project("copy");
	});

	setupGeneric([this, basePathName1, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"prefab"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"mesh"}, true));
	});
}

TEST_F(ExtrefTestWithZips, filecopy_paste_fail_new_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(
		basePathName1, [this]() {
			auto mesh = create<Mesh>("mesh");
		},
		std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		auto material = create<Material>("material");
		rename_project("copy");
	});

	setupGeneric([this, basePathName1, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"mesh"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"material"}, true));
	});
}

TEST_F(ExtrefTestWithZips, filecopy_paste_fail_same_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(
		basePathName1, [this]() {
			auto mesh = create<Mesh>("mesh");
		},
		std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		rename_project("copy");
	});

	setupGeneric([this, basePathName1, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName1, {"mesh"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"mesh"}, true));
	});
}

TEST_F(ExtrefTestWithZips, filecopy_update_fail_nested_same_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};
	auto midPathName((test_path() / "mid.rcp").string());
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(
		basePathName1, [this]() {
			auto mesh = create<Mesh>("mesh");
		},
		std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		rename_project("copy");
	});

	setupBase(
		midPathName, [this]() {
			auto prefab = create<Prefab>("prefab");
			auto meshnode = create<MeshNode>("prefab_child", prefab);
		},
		"mid");

	setupBase(
		compositePathName, [this, basePathName1, midPathName]() {
			ASSERT_TRUE(pasteFromExt(basePathName1, {"mesh"}, true));
			ASSERT_TRUE(pasteFromExt(midPathName, {"prefab"}, true));
		},
		"composite");

	updateBase(midPathName, [this, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName2, {"mesh"}, true));

		auto meshnode = find("prefab_child");
		auto mesh = findExt("mesh");
		cmd->set({meshnode, {"mesh"}}, mesh);
	});

	EXPECT_THROW(updateComposite(compositePathName, [this]() {}), raco::core::ExtrefError);
}

TEST_F(ExtrefTestWithZips, filecopy_update_fail_nested_different_object) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};
	auto midPathName((test_path() / "mid.rcp").string());
	auto compositePathName{(test_path() / "composite.rcp").string()};

	setupBase(
		basePathName1, [this]() {
			auto mesh = create<Mesh>("mesh");
			auto material = create<Material>("material");
		},
		std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		rename_project("copy");
	});

	setupBase(
		midPathName, [this]() {
			auto prefab = create<Prefab>("prefab");
			auto meshnode = create<MeshNode>("prefab_child", prefab);
		},
		"mid");

	setupBase(
		compositePathName, [this, basePathName1, midPathName]() {
			ASSERT_TRUE(pasteFromExt(basePathName1, {"material"}, true));
			ASSERT_TRUE(pasteFromExt(midPathName, {"prefab"}, true));
		},
		"composite");

	updateBase(midPathName, [this, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName2, {"mesh"}, true));

		auto meshnode = find("prefab_child");
		auto mesh = findExt("mesh");
		cmd->set({meshnode, {"mesh"}}, mesh);
	});

	EXPECT_THROW(updateComposite(compositePathName, [this]() {}), raco::core::ExtrefError);
}

TEST_F(ExtrefTestWithZips, extref_paste_fail_from_filecopy) {
	auto basePathName1{(test_path() / "base1.rcp").string()};
	auto basePathName2{(test_path() / "base2.rcp").string()};

	setupBase(
		basePathName1, [this]() {
			auto mesh = create<Mesh>("mesh");
		},
		std::string("base"));

	std::filesystem::copy(basePathName1, basePathName2);
	updateBase(basePathName2, [this]() {
		auto mesh = find("mesh");
		auto prefab = create<Prefab>("prefab");
		auto meshnode = create<MeshNode>("meshnode", prefab);
		cmd->set({meshnode, {"mesh"}}, mesh);
		auto material = create<Material>("material");
	});

	updateComposite(basePathName1, [this, basePathName2]() {
		ASSERT_FALSE(pasteFromExt(basePathName2, {"mesh"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"material"}, true));
		ASSERT_FALSE(pasteFromExt(basePathName2, {"prefab"}, true));
	});

	setupGeneric([this, basePathName2]() {
		ASSERT_TRUE(pasteFromExt(basePathName2, {"mesh"}, true));
		ASSERT_TRUE(pasteFromExt(basePathName2, {"material"}, true));
		ASSERT_TRUE(pasteFromExt(basePathName2, {"prefab"}, true));
	});
}
