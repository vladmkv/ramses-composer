/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This file is part of Ramses Composer
 * (see https://github.com/bmwcarit/ramses-composer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "object_tree_view_model/ObjectTreeViewDefaultModel.h"

#include "common_widgets/MeshAssetImportDialog.h"
#include "core/Context.h"
#include "core/CommandInterface.h"
#include "core/EditorObject.h"
#include "core/ExternalReferenceAnnotation.h"
#include "core/Project.h"
#include "core/Queries.h"
#include "object_tree_view_model/ObjectTreeNode.h"
#include "components/Naming.h"
#include "style/Colors.h"
#include "user_types/UserObjectFactory.h"
#include "object_creator/ObjectCreator.h"

#include <QApplication>
#include <QDataStream>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QMimeData>
#include <QProgressDialog>

namespace raco::object_tree::model {

using namespace raco::core;
using namespace raco::style;

ObjectTreeViewDefaultModel::ObjectTreeViewDefaultModel(core::CommandInterface* commandInterface,
	components::SDataChangeDispatcher dispatcher,
	core::ExternalProjectsStoreInterface* externalProjectStore,
	const std::vector<std::string>& allowedCreatableUserTypes,
	bool groupExternalReferences,
	bool groupByType)
	: dispatcher_{dispatcher},
	  commandInterface_{commandInterface},
	  externalProjectStore_{externalProjectStore},
	  allowedUserCreatableUserTypes_(allowedCreatableUserTypes),
	  groupExternalReferences_(groupExternalReferences),
	  groupByType_(groupByType) {
	resetInvisibleRootNode();

	std::sort(allowedUserCreatableUserTypes_.begin(), allowedUserCreatableUserTypes_.end());

	lifeCycleSubscription_ = dispatcher_->registerOnObjectsLifeCycle(
		[this](auto sEditorObject) { dirty_ = true; },
		[this](auto sEditorObject) { dirty_ = true; });

	afterDispatchSubscription_ = dispatcher_->registerOnAfterDispatch([this]() {
		if (dirty_) {
			buildObjectTree();
		}
	});

	auto setDirtyAction = [this](ValueHandle handle) {
		// Small optimization: Only set model dirty if the object with the changed name is actually in the model.
		if (indexes_.count(handle.rootObject()->objectID()) > 0) {
			dirty_ = true;
		}
	};

	objectNameSubscription_ = dispatcher_->registerOnPropertyChange("objectName", setDirtyAction);
	visibilitySubscription_ = dispatcher_->registerOnPropertyChange("visibility", setDirtyAction);
	enabledSubscription_ = dispatcher_->registerOnPropertyChange("enabled", setDirtyAction);
	editorVisibilitySubscription_ = dispatcher_->registerOnPropertyChange("editorVisibility", setDirtyAction);

	childrenSubscription_ = dispatcher_->registerOnPropertyChange("children", [this](ValueHandle handle) {
		dirty_ = true;
	});
	rootOrderSubscription_ = dispatcher_->registerOnRootOrderChanged([this]() {
		dirty_ = true;
	});

	extProjectChangedSubscription_ = dispatcher_->registerOnExternalProjectMapChanged([this]() { dirty_ = true; });

	dirty_ = true;
}

int ObjectTreeViewDefaultModel::columnCount(const QModelIndex& parent) const {
	return COLUMNINDEX_COLUMN_COUNT;
}


QVariant ObjectTreeViewDefaultModel::getNodeIcon(ObjectTreeNode* treeNode) const {
	std::string typeName;
	if (treeNode->getType() == ObjectTreeNodeType::TypeParent) {
		typeName = treeNode->getTypeName();
	} else {
		auto editorObj = treeNode->getRepresentedObject();
		if (editorObj) {
			typeName = editorObj->getTypeDescription().typeName;
		}
	}

	if (!typeName.empty()) {
		auto itr = typeIconMap.find(typeName);
		if (itr == typeIconMap.end())
			return QVariant();
		return QVariant(itr->second);
	}
	return QVariant();
}

QVariant ObjectTreeViewDefaultModel::getPreviewVisibilityIcon(ObjectTreeNode* treeNode) const {
	auto it = visibilityIconMap_.find(treeNode->getPreviewVisibility());
	return it != visibilityIconMap_.end() ? it->second : QVariant();
}

QVariant ObjectTreeViewDefaultModel::getAbstractViewVisibilityIcon(ObjectTreeNode* treeNode) const {
	auto it = visibilityIconMap_.find(treeNode->getAbstractViewVisibility());
	return it != visibilityIconMap_.end() ? it->second : QVariant();
}

QVariant ObjectTreeViewDefaultModel::data(const QModelIndex& index, int role) const {
	if (!index.isValid()) {
		return QVariant();
	}

	auto treeNode = indexToTreeNode(index);

	switch (role) {
		case Qt::DecorationRole: {
			if (index.column() == COLUMNINDEX_NAME) {
				return getNodeIcon(treeNode);
			} else if (index.column() == COLUMNINDEX_PREVIEW_VISIBILITY) {
				return getPreviewVisibilityIcon(treeNode);
			} else if (index.column() == COLUMNINDEX_ABSTRACT_VIEW_VISIBILITY) {
				return getAbstractViewVisibilityIcon(treeNode);
			}
			return QVariant();
		}
		case Qt::FontRole: {
			if (treeNode->getType() == ObjectTreeNodeType::TypeParent) {
				QFont font;
				font.setItalic(true);
				return QVariant(font);
			}

			return QVariant();
		}
		case Qt::ForegroundRole: {
			auto editorObj = treeNode->getRepresentedObject();
			if (editorObj && editorObj->query<ExternalReferenceAnnotation>()) {
				return QVariant(Colors::color(Colormap::externalReference));
			}

			if (treeNode->getType() == ObjectTreeNodeType::ExtRefGroup) {
				return QVariant(Colors::color(Colormap::externalReferenceDisabled));
			}

			if (editorObj && Queries::isReadOnly(editorObj)) {
				return QVariant(Colors::color(Colormap::textDisabled));
			}

			if (treeNode->getType() == ObjectTreeNodeType::TypeParent) {
				return treeNode->getParent()->getType() == ObjectTreeNodeType::ExtRefGroup
						   ? QVariant(Colors::color(Colormap::externalReferenceDisabled))
						   : QVariant(Colors::color(Colormap::textDisabled));
			}

			return QVariant(Colors::color(Colormap::text));
		}
		case Qt::DisplayRole: {
			switch (index.column()) {
				case COLUMNINDEX_NAME:
					return QVariant(QString::fromStdString(treeNode->getDisplayName()));
				case COLUMNINDEX_TYPE:
					return QVariant(QString::fromStdString(treeNode->getDisplayType()));
				case COLUMNINDEX_ID:
					return QVariant(QString::fromStdString(treeNode->getID()));
				case COLUMNINDEX_USERTAGS: {
					QStringList qtags;
					for (const auto& tag : treeNode->getUserTags()) {
						qtags.append(QString::fromStdString(tag));
					}
					return QVariant(qtags.join(", "));
				}
				case COLUMNINDEX_PROJECT:
					return QVariant(QString::fromStdString(treeNode->getExternalProjectName()));
				case COLUMNINDEX_PREVIEW_VISIBILITY:
					return QVariant(QString());
				case COLUMNINDEX_ABSTRACT_VIEW_VISIBILITY:
					return QVariant(QString());
			}
		}
		case Qt::EditRole: {
			return QVariant(QString::fromStdString(treeNode->getDisplayName()));
		}
	}

	return QVariant();
}

QVariant ObjectTreeViewDefaultModel::headerData(int section, Qt::Orientation orientation, int role) const {
	switch (role) {
		case Qt::DisplayRole: {
			switch (section) {
				case COLUMNINDEX_NAME:
					return QVariant("Name");
				case COLUMNINDEX_TYPE:
					return QVariant("Type");
				case COLUMNINDEX_ID:
					return QVariant("ID");
				case COLUMNINDEX_USERTAGS:
					return QVariant("User Tags");
				case COLUMNINDEX_PROJECT:
					return QVariant("Project Name");
			}
		} break;
		case Qt::DecorationRole:
			if (section == COLUMNINDEX_PREVIEW_VISIBILITY) {
				return style::Icons::instance().screenshot;
			} else if (section == COLUMNINDEX_ABSTRACT_VIEW_VISIBILITY) {
				return style::Icons::instance().abstractSceneView;
			}
			break;
	}

	return QVariant();
}

QModelIndex ObjectTreeViewDefaultModel::index(int row, int column, const QModelIndex& parent) const {
	auto* parentNode = indexToTreeNode(parent);

	if (!parentNode) {
		return {};
	} else {
		return createIndex(row, column, parentNode->getChild(row));
	}
}

QList<QFileInfo> ObjectTreeViewDefaultModel::getAcceptableFilesInfo(const QMimeData* data) const {
	if (data->urls().empty()) {
		return {};
	}

	QList<QFileInfo> fileInfos;
	for (const auto& url : data->urls()) {
		const QString filePath = url.toLocalFile();
		if (!filePath.isEmpty()) {
			const QFileInfo fileInfo{QFile{filePath}};
			const auto fileExtension = fileInfo.suffix().toLower();
			if (acceptableFileExtensions_.contains(fileExtension)) {
				fileInfos.push_front(fileInfo);
			}
		}
	}

	return fileInfos;
}

bool ObjectTreeViewDefaultModel::canDropMimeData(const QMimeData* data, Qt::DropAction action, int row, int column, const QModelIndex& parent) const {
	if (action == Qt::IgnoreAction) {
		return false;
	}

	if (data->hasFormat(OBJECT_EDITOR_ID_MIME_TYPE)) {
		auto idList{decodeMimeData(data)};
		auto dragDroppingProjectNode = externalProjectStore_->isExternalProject(idList.front().toStdString());

		if (dragDroppingProjectNode) {
			return false;
		}

		auto originPath = getOriginPathFromMimeData(data);
		auto droppingFromOtherProject = originPath != project()->currentPath();
		auto parentType = indexToTreeNode(parent)->getType();
		auto droppingAsExternalReference = QGuiApplication::queryKeyboardModifiers().testFlag(Qt::KeyboardModifier::AltModifier) ||
										   parentType == ObjectTreeNodeType::ExtRefGroup;
		if (droppingAsExternalReference && (!droppingFromOtherProject || parentType == ObjectTreeNodeType::EditorObject)) {
			return false;
		}

		if (idList.empty()) {
			return false;
		}

		if (!droppingFromOtherProject) {
			std::vector<SEditorObject> objs;
			for (const auto& id : idList) {
				objs.emplace_back(project()->getInstanceByID(id.toStdString()));
			}

			return (std::any_of(objs.begin(), objs.end(), [this, parent](auto obj) { return isObjectAllowedIntoIndex(parent, obj); }) &&
					!Queries::filterForMoveableScenegraphChildren(*project(), objs, indexToSEditorObject(parent)).empty());

		} else {
			std::vector<SEditorObject> objectsFromId;
			std::set<std::string> sourceProjectTopLevelObjectIds;
			auto originCommandInterface = externalProjectStore_->getExternalProjectCommandInterface(originPath);
			for (const auto& id : idList) {
				auto idString = id.toStdString();
				auto objectFromId = Queries::findById(*originCommandInterface->project(), idString);

				if (objectFromId->getParent() == nullptr) {
					sourceProjectTopLevelObjectIds.emplace(idString);
				}
				objectsFromId.emplace_back(objectFromId);
			}
			return canPasteIntoIndex(parent, objectsFromId, sourceProjectTopLevelObjectIds, droppingAsExternalReference);
		}
	}
	else if (!acceptableFileExtensions_.empty() && !getAcceptableFilesInfo(data).empty()) {
		return true;
	}

	return false;
}

QModelIndex ObjectTreeViewDefaultModel::parent(const QModelIndex& child) const {
	if (!child.isValid()) {
		return {};
	}

	auto* childNode = indexToTreeNode(child);
	auto* parentNode = childNode->getParent();

	if (parentNode) {
		if (parentNode == invisibleRootNode_.get()) {
			return {};
		}
		return createIndex(parentNode->row(), COLUMNINDEX_NAME, parentNode);
	}

	return {};
}

Qt::DropActions ObjectTreeViewDefaultModel::supportedDropActions() const {
	return Qt::MoveAction | Qt::CopyAction;
}

std::string ObjectTreeViewDefaultModel::getOriginPathFromMimeData(const QMimeData* data) const {
	QByteArray encodedData = data->data(OBJECT_EDITOR_ID_MIME_TYPE);
	QDataStream stream(&encodedData, QIODevice::ReadOnly);

	QString mimeDataOriginProjectPath;
	stream >> mimeDataOriginProjectPath;

	return mimeDataOriginProjectPath.toStdString();
}

QMimeData* object_tree::model::ObjectTreeViewDefaultModel::generateMimeData(const QModelIndexList& indexes, const std::string& originPath) const {
	QMimeData* mimeData = new QMimeData();
	QByteArray encodedData;

	QDataStream stream(&encodedData, QIODevice::WriteOnly);
	stream << QString::fromStdString(originPath);
	for (const auto& index : indexes) {
		if (index.column() == COLUMNINDEX_NAME) {
			if (auto obj = indexToSEditorObject(index)) {
				// Object ID
				stream << QString::fromStdString(obj->objectID());
			}
		}
	}

	mimeData->setData(OBJECT_EDITOR_ID_MIME_TYPE, encodedData);
	return mimeData;
}

QStringList ObjectTreeViewDefaultModel::decodeMimeData(const QMimeData* data) const {
	QByteArray encodedData = data->data(OBJECT_EDITOR_ID_MIME_TYPE);
	QDataStream stream(&encodedData, QIODevice::ReadOnly);
	QStringList itemIDs;

	// Skipping
	QString mimeDataOriginProjectPath;
	stream >> mimeDataOriginProjectPath;

	while (!stream.atEnd()) {
		QString text;
		stream >> text;
		itemIDs << text;
	}

	return itemIDs;
}

bool ObjectTreeViewDefaultModel::dropMimeData(const QMimeData* data, Qt::DropAction action,
	int row, int column, const QModelIndex& parent) {
	if (action == Qt::IgnoreAction) {
		return true;
	}

	if (data->hasFormat(OBJECT_EDITOR_ID_MIME_TYPE)) {
		auto originPath = getOriginPathFromMimeData(data);
		auto mimeDataContainsLocalInstances = originPath == project()->currentPath();
		auto movedItemIDs = decodeMimeData(data);

		if (mimeDataContainsLocalInstances) {
			std::vector<SEditorObject> objs;
			for (const auto& movedItemID : movedItemIDs) {
				if (auto childObj = project()->getInstanceByID(movedItemID.toStdString())) {
					objs.emplace_back(childObj);
				}
			}

			moveScenegraphChildren(objs, indexToSEditorObject(parent), row);
		} else {
			auto originCommandInterface = externalProjectStore_->getExternalProjectCommandInterface(originPath);
			std::vector<SEditorObject> objs;
			for (const auto& movedItemID : movedItemIDs) {
				if (auto externalProjectObj = originCommandInterface->project()->getInstanceByID(movedItemID.toStdString())) {
					objs.emplace_back(externalProjectObj);
				}
			}
			auto serializedObjects = originCommandInterface->copyObjects(objs, true);

			auto pressedKeys = QGuiApplication::queryKeyboardModifiers();
			auto pasteAsExtRef = pressedKeys.testFlag(Qt::KeyboardModifier::AltModifier) || indexToTreeNode(parent)->getType() == ObjectTreeNodeType::ExtRefGroup;
			pasteObjectAtIndex(parent, pasteAsExtRef, nullptr, serializedObjects);
			return true;
		}

	} else if (!acceptableFileExtensions_.empty() && !getAcceptableFilesInfo(data).empty()) {
		for (const auto& fileInfo : getAcceptableFilesInfo(data)) {
			dropFile(fileInfo, parent);
		}
		return true;
	}
	return false;
}

Qt::ItemFlags ObjectTreeViewDefaultModel::flags(const QModelIndex& index) const {
	Qt::ItemFlags itemFlags = QAbstractItemModel::flags(index) | Qt::ItemIsDropEnabled;

	if (auto obj = indexToSEditorObject(index); obj && !obj->query<ExternalReferenceAnnotation>()) {
		itemFlags |= Qt::ItemIsDragEnabled;
		if (!Queries::isReadOnly(*project(), core::ValueHandle{obj, {"objectName"}}) && index.column() == COLUMNINDEX_NAME) {
			itemFlags |= Qt::ItemIsEditable;
		}
	}

	return itemFlags;
}

QMimeData* ObjectTreeViewDefaultModel::mimeData(const QModelIndexList& indices) const {

	// Sort the list to make sure order after dropping remains consistent, regardless of what selectiong order is.
	auto sortedList = indices.toVector();
	std::sort(sortedList.begin(), sortedList.end(), ObjectTreeViewDefaultModel::isIndexAboveInHierachyOrPosition);

	return generateMimeData(QModelIndexList(sortedList.begin(), sortedList.end()), project()->currentPath());
}

QStringList ObjectTreeViewDefaultModel::mimeTypes() const {
	return {OBJECT_EDITOR_ID_MIME_TYPE};
}

void ObjectTreeViewDefaultModel::buildObjectTree() {
	dirty_ = false;
	if (!commandInterface_) {
		return;
	}

	auto filteredEditorObjects = filterForTopLevelObjects(project()->instances());

	beginResetModel();

	resetInvisibleRootNode();
	constructTreeUnderNode(invisibleRootNode_.get(), filteredEditorObjects, groupExternalReferences_, groupByType_);
	updateTreeIndexes();

	endResetModel();
}

void ObjectTreeViewDefaultModel::setNodeExternalProjectInfo(ObjectTreeNode* node) const {
	if (auto obj = node->getRepresentedObject()) {
		if (auto extrefAnno = obj->query<ExternalReferenceAnnotation>()) {
			node->setBelongsToExternalProject(
				project()->lookupExternalProjectPath(*extrefAnno->projectID_),
				project()->lookupExternalProjectName(*extrefAnno->projectID_));
		}
	}
}

void ObjectTreeViewDefaultModel::ensureTypeParentExists(std::map<std::string, ObjectTreeNode*>& typeParentMap, const std::string& typeName, ObjectTreeNode* parentNode) {
	if (typeParentMap.find(typeName) != typeParentMap.end()) {
		return;
	}

	auto* parent = new ObjectTreeNode(typeName);
	typeParentMap[typeName] = parent;
	parentNode->addChildFront(parent);
}

void ObjectTreeViewDefaultModel::constructTreeUnderNode(ObjectTreeNode* rootNode, const std::vector<core::SEditorObject>& children, bool groupExternalReferences, bool groupByTypes) {
	auto rootObject = rootNode->getRepresentedObject();
	auto* extRefParent = groupExternalReferences ? nullptr : rootNode;
	std::map<std::string, ObjectTreeNode*> extRefTypeParents;
	std::map<std::string, ObjectTreeNode*> typeParents;

	for (const auto& obj : children) {
		auto* parentNode = rootNode;

		if (obj->query<ExternalReferenceAnnotation>()) {
			if (extRefParent == nullptr) {
				extRefParent = new ObjectTreeNode(ObjectTreeNodeType::ExtRefGroup, nullptr);
				rootNode->addChildFront(extRefParent);
			}
			if (groupByTypes) {
				ensureTypeParentExists(extRefTypeParents, obj->getTypeDescription().typeName, extRefParent);
				parentNode = extRefTypeParents[obj->getTypeDescription().typeName];
			} else {
				parentNode = extRefParent;
			}
		} else if (groupByTypes) {
			ensureTypeParentExists(typeParents, obj->getTypeDescription().typeName, rootNode);
			parentNode = typeParents[obj->getTypeDescription().typeName];
		}

		auto* node = new ObjectTreeNode(obj, parentNode);
		setNodeExternalProjectInfo(node);
		constructTreeUnderNode(node, obj->children_->asVector<SEditorObject>(), false, false);
	}
}

std::vector<SEditorObject> ObjectTreeViewDefaultModel::filterForTopLevelObjects(const std::vector<SEditorObject>& objects) const {
	return Queries::filterForTopLevelObjectsByTypeName(objects, allowedUserCreatableUserTypes_);
}

std::vector<std::string> ObjectTreeViewDefaultModel::creatableTypes(const QModelIndex& parent) const {
	std::vector<std::string> result;
	for (auto typeName : typesAllowedIntoIndex(parent)) {
		if (objectFactory()->isUserCreatable(typeName, project()->featureLevel())) {
			result.emplace_back(typeName);
		}
	}
	return result;
}

SEditorObject ObjectTreeViewDefaultModel::createNewObject(const std::string& typeName, const std::string& nodeName, const QModelIndex& parent) {
	SEditorObject parentObj = indexToSEditorObject(parent);

	std::vector<SEditorObject> nodes;
	std::copy_if(project()->instances().begin(), project()->instances().end(), std::back_inserter(nodes), [this, parentObj](const SEditorObject& obj) {
		return obj->getParent() == parentObj;
	});

	auto name = project()->findAvailableUniqueName(nodes.begin(), nodes.end(), nullptr, nodeName.empty() ? components::Naming::format(typeName) : nodeName);
	auto newObj = commandInterface_->createObject(typeName, name, parent.isValid() ? parentObj : nullptr);

	return newObj;
}

void ObjectTreeViewDefaultModel::dropFile(const QFileInfo& fileInfo, const QModelIndex& parent) {
	if (gltfDropOpensAssetImportDialog_ && (fileInfo.suffix().toLower() == "glb" || fileInfo.suffix().toLower() == "gltf")) {
		importMeshScenegraph(fileInfo.absoluteFilePath(), parent);
	} else {
		object_creator::ObjectCreator objCreator(commandInterface_, acceptLuaModules_, acceptLuaInterfaces_, acceptLuaScripts_);
		objCreator.createNewObjectFromFile(fileInfo);
	}
}

bool ObjectTreeViewDefaultModel::canCopyAtIndices(const QModelIndexList& indices) const {
	for (const auto& index : indices) {
		if (indexToSEditorObject(index)) {
			return true;
		}
	}
	return false;
}

bool ObjectTreeViewDefaultModel::canDeleteAtIndices(const QModelIndexList& indices) const {
	return !Queries::filterForDeleteableObjects(*commandInterface_->project(), indicesToSEditorObjects(indices)).empty();
}

bool ObjectTreeViewDefaultModel::isObjectAllowedIntoIndex(const QModelIndex& index, const core::SEditorObject& obj) const {
	if (auto parentObj = indexToSEditorObject(index); parentObj && !core::Queries::canPasteIntoObject(*commandInterface_->project(), parentObj)) {
		return false;
	}

	auto types = typesAllowedIntoIndex(index);

	return std::find(types.begin(), types.end(), obj->getTypeDescription().typeName) != types.end();
}

std::pair<std::vector<core::SEditorObject>, std::set<std::string>> ObjectTreeViewDefaultModel::getObjectsAndRootIdsFromClipboardString(const std::string& serializedObjs) const {
	auto deserialization{serialization::deserializeObjects(serializedObjs)};
	if (deserialization) {
		auto objects = BaseContext::getTopLevelObjectsFromDeserializedObjects(*deserialization, project());
		return {objects, deserialization->rootObjectIDs};
	}
	return {};
}

bool ObjectTreeViewDefaultModel::canPasteIntoIndex(const QModelIndex& index, const std::vector<core::SEditorObject>& objects, const std::set<std::string>& sourceProjectTopLevelObjectIds, bool asExtRef) const {
	if (asExtRef) {
		// Only allow top level extref pasting
		if (indexToSEditorObject(index)) {
			return false;
		}

		// Allow pasting objects if any objects fits the location.
		for (const auto& obj : objects) {
			if (Queries::canPasteObjectAsExternalReference(obj, sourceProjectTopLevelObjectIds.find(obj->objectID()) != sourceProjectTopLevelObjectIds.end()) &&
				isObjectAllowedIntoIndex(index, obj)) {
				return true;
			}
		}
	}
	else {
		// Allow pasting objects if any objects fits the location.
		for (const auto& obj : objects) {
			if (isObjectAllowedIntoIndex(index, obj)) {
				return true;
			}
		}
	}


	return false;
}

bool ObjectTreeViewDefaultModel::canDuplicateAtIndices(const QModelIndexList& indices) const {
	if (indices.empty()) {
		return false;
	}

	std::vector<SEditorObject> objs;

	for (const auto& index : indices) {
		auto obj = indexToSEditorObject(index);
		if (!obj) {
			return false;
		}

		objs.emplace_back(obj);
	}

	return Queries::canDuplicateObjects(objs, *project());
}

size_t ObjectTreeViewDefaultModel::deleteObjectsAtIndices(const QModelIndexList& indices) {
	return commandInterface_->deleteObjects(indicesToSEditorObjects(indices));
}

bool ObjectTreeViewDefaultModel::canDeleteUnusedResources() const {
	return core::Queries::canDeleteUnreferencedResources(*commandInterface_->project());
}

bool ObjectTreeViewDefaultModel::canProgramaticallyGotoObject() const {
	return true;
}

void ObjectTreeViewDefaultModel::deleteUnusedResources() {
	commandInterface_->deleteUnreferencedResources();
}

void ObjectTreeViewDefaultModel::isolateNodes(const QModelIndexList& indices) {
	auto objects = Queries::collectAllChildren(indicesToSEditorObjects(indices));
	for (auto instance : commandInterface_->project()->instances()) {
		if (auto node = instance->as<user_types::Node>()) {
			if (std::find(objects.begin(), objects.end(), node) == objects.end()) {
				commandInterface_->set({node, &user_types::Node::editorVisibility_}, false);
			}
		}
	}
}

void ObjectTreeViewDefaultModel::hideNodes(const QModelIndexList& indices) {
	auto objects = Queries::collectAllChildren(indicesToSEditorObjects(indices));
	for (auto object : objects) {
		if (auto node = object->as<user_types::Node>()) {
			commandInterface_->set({node, &user_types::Node::editorVisibility_}, false);
		}
	}
}

void ObjectTreeViewDefaultModel::showAllNodes() {
	for (auto instance : commandInterface_->project()->instances()) {
		if (auto node = instance->as<user_types::Node>()) {
			commandInterface_->set({node, &user_types::Node::editorVisibility_}, true);
		}
	}
}

void ObjectTreeViewDefaultModel::copyObjectsAtIndices(const QModelIndexList& indices, bool deepCopy) {
	auto objects = indicesToSEditorObjects(indices);
	RaCoClipboard::set(commandInterface_->copyObjects(objects, deepCopy));
}

bool ObjectTreeViewDefaultModel::pasteObjectAtIndex(const QModelIndex& index, bool pasteAsExtref, std::string* outError, const std::string& serializedObjects) {
	bool success = true;
	try {
		commandInterface_->pasteObjects(serializedObjects, indexToSEditorObject(index), pasteAsExtref);
	} catch (std::exception& error) {
		success = false;
		if (outError) {
			*outError = error.what();
		}
	}
	return success;
}

std::vector<SEditorObject> ObjectTreeViewDefaultModel::duplicateObjectsAtIndices(const QModelIndexList& indices) {
	auto objects = indicesToSEditorObjects(indices);
	return commandInterface_->duplicateObjects(objects);
}

void ObjectTreeViewDefaultModel::cutObjectsAtIndices(const QModelIndexList& indices, bool deepCut) {
	auto objects = indicesToSEditorObjects(indices);
	auto text = commandInterface_->cutObjects(objects, deepCut);
	if (!text.empty()) {
		RaCoClipboard::set(text);
	}
}

void ObjectTreeViewDefaultModel::moveScenegraphChildren(const std::vector<SEditorObject>& objects, SEditorObject parent, int row) {
	int insertBeforeIndex = row;
	if (!parent) {
		// For top-level target objects we need to translate the row into the index in the Project::instances() vector:
		auto insertionObject = indexToSEditorObject(index(row, 0, invisibleRootIndex_));
		if (insertionObject) {
			auto it = std::find(commandInterface_->project()->instances().begin(), commandInterface_->project()->instances().end(), insertionObject);
			insertBeforeIndex = it - commandInterface_->project()->instances().begin();
		} else {
			insertBeforeIndex = -1;
		}
	}
	commandInterface_->moveScenegraphChildren(objects, parent, insertBeforeIndex);
}

void ObjectTreeViewDefaultModel::importMeshScenegraph(const QString& filePath, const QModelIndex& selectedIndex) {
	auto absPath = filePath.toStdString();

	auto selectedObject = indexToSEditorObject(selectedIndex);

	// create dummy cache entry to prevent "cache corpses" if the mesh file is otherwise not accessed by any Mesh
	auto dummyCacheEntry = commandInterface_->meshCache()->registerFileChangedHandler(absPath, {nullptr, nullptr});
	if (auto sceneGraphPtr = commandInterface_->meshCache()->getMeshScenegraph(absPath)) {
		MeshScenegraph sceneGraph{*sceneGraphPtr};
		auto importStatus = common_widgets::MeshAssetImportDialog(sceneGraph, QFileInfo(filePath).fileName(), nullptr).exec();
		if (importStatus == QDialog::Accepted) {
			commandInterface_->insertAssetScenegraph(sceneGraph, absPath, selectedObject);
		}
	} else {
		auto meshError = commandInterface_->meshCache()->getMeshError(absPath);
		Q_EMIT meshImportFailed(absPath, meshError);
	}
}

int ObjectTreeViewDefaultModel::rowCount(const QModelIndex& parent) const {
	if (auto* parentNode = indexToTreeNode(parent)) {
		return static_cast<int>(parentNode->childCount());
	}
	return 0;
}

void ObjectTreeViewDefaultModel::iterateThroughTree(std::function<void(QModelIndex&)> nodeFunc, QModelIndex& currentIndex) {
	if (currentIndex.row() != -1) {
		nodeFunc(currentIndex);
	}
	for (int i = 0; i < rowCount(currentIndex); ++i) {
		auto childIndex = index(i, 0, currentIndex);
		iterateThroughTree(nodeFunc, childIndex);
	}
}

ObjectTreeNode* ObjectTreeViewDefaultModel::indexToTreeNode(const QModelIndex& index) const {
	if (index.isValid()) {
		if (auto* node = static_cast<ObjectTreeNode*>(index.internalPointer())) {
			return node;
		}
	}
	return invisibleRootNode_.get();
}

SEditorObject ObjectTreeViewDefaultModel::indexToSEditorObject(const QModelIndex& index) const {
	return indexToTreeNode(index)->getRepresentedObject();
}

std::vector<core::SEditorObject> ObjectTreeViewDefaultModel::indicesToSEditorObjects(const QModelIndexList& indices) const {
	std::vector<SEditorObject> objects;
	for (const auto& index : indices) {
		if (auto obj = indexToSEditorObject(index); obj) {
			objects.push_back(obj);
		}
	}
	return objects;
}

QModelIndex ObjectTreeViewDefaultModel::indexFromTreeNodeID(const std::string& id) const {
	auto cachedID = indexes_.find(id);
	if (cachedID != indexes_.end()) {
		return cachedID->second;
	}

	return QModelIndex();
}

void ObjectTreeViewDefaultModel::resetInvisibleRootNode() {
	invisibleRootNode_ = std::make_unique<ObjectTreeNode>(ObjectTreeNodeType::Root, nullptr);
}

void object_tree::model::ObjectTreeViewDefaultModel::updateTreeIndexes() {
	indexes_.clear();
	iterateThroughTree([&](const auto& modelIndex) {
		indexes_[indexToTreeNode(modelIndex)->getID()] = modelIndex;
	},
		invisibleRootIndex_);
}

UserObjectFactoryInterface* ObjectTreeViewDefaultModel::objectFactory() const {
	return commandInterface_->objectFactory();
}

Project* ObjectTreeViewDefaultModel::project() const {
	return commandInterface_->project();
}

Qt::TextElideMode ObjectTreeViewDefaultModel::textElideMode() const {
	return Qt::TextElideMode::ElideRight;
}

bool ObjectTreeViewDefaultModel::isIndexAboveInHierachyOrPosition(QModelIndex left, QModelIndex right) {
	while (left.parent() != right.parent()) {
		left = left.parent();
		right = right.parent();

		if (!left.isValid()) {
			return true;
		} else if (!right.isValid()) {
			return false;
		}
	}

	return left.row() < right.row();
}

std::vector<std::string> object_tree::model::ObjectTreeViewDefaultModel::typesAllowedIntoIndex(const QModelIndex& index) const {
	auto object = indexToSEditorObject(index);
	if (!object) {
		return allowedUserCreatableUserTypes_;
	} else {
		if (!core::Queries::canPasteIntoObject(*commandInterface_->project(), object)) {
			return {};
		} else {
			return core::Queries::validTypesForChildrenOf(object);
		}
	}
}

std::string ObjectTreeViewDefaultModel::objectIdAtIndex(const QModelIndex& index) const {
	auto id = indexToTreeNode(index)->getID();
	if (!id.empty()) {
		return id;
	}
	return {};
}

std::string ObjectTreeViewDefaultModel::externalProjectPathAtIndex(const QModelIndex& index) const {
	auto path = indexToTreeNode(index)->getExternalProjectPath();
	if (!path.empty()) {
		return path;
	}
	return {};
}

std::set<std::string> ObjectTreeViewDefaultModel::externalProjectPathsAtIndices(const QModelIndexList& indices) const {
	std::set<std::string> projectPaths;
	for (const auto& index : indices) {
		auto path = indexToTreeNode(index)->getExternalProjectPath();
		if (!path.empty()) {
			projectPaths.emplace(path);
		}
	}
	return projectPaths;
}

void ObjectTreeViewDefaultModel::setAcceptableFileExtensions(const QStringList& extensions) {
	acceptableFileExtensions_ = extensions;
}

void ObjectTreeViewDefaultModel::setAcceptLuaModules(bool accept) {
	acceptLuaModules_ = accept;
}

void ObjectTreeViewDefaultModel::setAcceptLuaScripts(bool accept) {
	acceptLuaScripts_ = accept;
}

void ObjectTreeViewDefaultModel::setAcceptLuaInterfaces(bool accept) {
	acceptLuaInterfaces_ = accept;
}

void ObjectTreeViewDefaultModel::setDropGltfOpensAssetImportDialog(bool flag) {
	gltfDropOpensAssetImportDialog_ = flag;
}

bool ObjectTreeViewDefaultModel::setData(const QModelIndex& index, const QVariant& value, int role) {
	bool success{false};

	if (auto obj = indexToSEditorObject(index)) {
		if (Queries::isReadOnly(obj)) {
			assert(false && "trying to set a read-only object!");
			return false;
		}

		if (role == Qt::EditRole) {
			if (obj->objectName() == value.toString().toStdString()) {
				success = false;
			} else {
				commandInterface_->set(core::ValueHandle{obj, {"objectName"}}, value.toString().toStdString());
				if (obj->objectName() == value.toString().toStdString()) {
					success = true;
				} else {
					assert(false && "model failed to set object name!");
					success = false;
				}
			}
		}
	} else {
		success = QAbstractItemModel::setData(index, value, role);
	}

	if (success) {
		Q_EMIT dataChanged(index, index);
	}

	return success;
}

}  // namespace raco::object_tree::model