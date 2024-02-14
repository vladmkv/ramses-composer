/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This file is part of Ramses Composer
 * (see https://github.com/bmwcarit/ramses-composer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */

#include "object_tree_view_model/ObjectTreeViewSortProxyModels.h"
#include "object_tree_view_model/ObjectTreeNode.h"
#include "object_tree_view_model/ObjectTreeViewDefaultModel.h"
#include "style/Colors.h"

#include <QColor>

namespace raco::object_tree::model {

ObjectTreeViewDefaultSortFilterProxyModel::ObjectTreeViewDefaultSortFilterProxyModel(QObject* parent, bool enableSorting) : QSortFilterProxyModel(parent), sortingEnabled_(enableSorting) {
	setFilterCaseSensitivity(Qt::CaseSensitivity::CaseInsensitive);
	setRecursiveFilteringEnabled(true);
}


bool ObjectTreeViewDefaultSortFilterProxyModel::sortingEnabled() const {
	return sortingEnabled_;
}

void ObjectTreeViewDefaultSortFilterProxyModel::setCustomFilter(std::function<bool(const ObjectTreeNode&)> filterFunc) {
	customFilter = filterFunc;
	invalidateFilter();
}

void ObjectTreeViewDefaultSortFilterProxyModel::removeCustomFilter() {
	customFilter = nullptr;
	invalidateFilter();
}

QString ObjectTreeViewDefaultSortFilterProxyModel::getDataAtIndex(const QModelIndex& index) const {
	return data(index, Qt::DisplayRole).toString();
}

QVariant ObjectTreeViewDefaultSortFilterProxyModel::data(const QModelIndex& index, int role) const {
	if (index.isValid()) {
		ObjectTreeNode* treeNode = static_cast<ObjectTreeNode*>(mapToSource(index).internalPointer());
		const auto nodeType = treeNode->getType();
		if (nodeType == ObjectTreeNodeType::ExtRefGroup || nodeType == ObjectTreeNodeType::TypeParent) {
			return QSortFilterProxyModel::data(index, role);
		}

		if (role == Qt::ForegroundRole) {
			if (customFilter && treeNode && !customFilter(*treeNode)) {
				return QVariant(style::Colors::color(style::Colormap::textDisabled).darker(175));
			}
		}
	}

	return QSortFilterProxyModel::data(index, role);
}

bool ObjectTreeViewDefaultSortFilterProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex& sourceParent) const {
	const auto treeNodeIndex = sourceModel()->index(sourceRow, 0, sourceParent);
	const auto treeNode = static_cast<ObjectTreeNode*>(treeNodeIndex.internalPointer());
	if (customFilter && treeNode) {
		return customFilter(*treeNode);
	}
	return QSortFilterProxyModel::filterAcceptsRow(sourceRow, sourceParent);
}

bool ObjectTreeViewResourceSortFilterProxyModel::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const {
	auto leftType = static_cast<ObjectTreeNode*>(source_left.internalPointer())->getType();
	if (leftType == ObjectTreeNodeType::ExtRefGroup) {
		return sortOrder() == Qt::SortOrder::AscendingOrder;
	}

	auto rightType = static_cast<ObjectTreeNode*>(source_right.internalPointer())->getType();
	if (rightType == ObjectTreeNodeType::ExtRefGroup) {
		return sortOrder() == Qt::SortOrder::DescendingOrder;
	}

	if (leftType == rightType) {
		if (leftType == ObjectTreeNodeType::TypeParent) {
			auto left = static_cast<ObjectTreeNode*>(source_left.internalPointer())->getDisplayName();
			auto right = static_cast<ObjectTreeNode*>(source_right.internalPointer())->getDisplayName();

			return sortOrder() == Qt::SortOrder::AscendingOrder
					   ? left < right
					   : left > right;
		}

		return QSortFilterProxyModel::lessThan(source_left, source_right);
	}

	return false;
}

bool ObjectTreeViewTopLevelSortFilterProxyModel::lessThan(const QModelIndex& source_left, const QModelIndex& source_right) const {
	// The external reference grouping element should always be on top of the list
	if (static_cast<ObjectTreeNode*>(source_left.internalPointer())->getType() == ObjectTreeNodeType::ExtRefGroup) {
		return true;
	} else if (static_cast<ObjectTreeNode*>(source_right.internalPointer())->getType() == ObjectTreeNodeType::ExtRefGroup) {
		return false;
	}

	// Only compare items that are on the same level and only sort items that are below the root node or the ExtRefGroup
	if (source_left.parent() == source_right.parent() && (!source_left.parent().isValid() || static_cast<ObjectTreeNode*>(source_left.parent().internalPointer())->getType() == ObjectTreeNodeType::ExtRefGroup)) {
		return QSortFilterProxyModel::lessThan(source_left, source_right);
	}

	return false;
}

}  // namespace raco::object_tree::model