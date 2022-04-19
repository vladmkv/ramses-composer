/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This file is part of Ramses Composer
 * (see https://github.com/GENIVI/ramses-composer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "EditorObject.h"
#include "Handles.h"
#include "Link.h"

#include <map>
#include <vector>

namespace raco::core {

class Project;

namespace Queries {

	// Find all reference properties of instances in the project outside the argument object set 
	// which point into the object set.
	std::vector<ValueHandle> findAllReferencesTo(Project const &project, std::vector<SEditorObject> const& objects);

	std::vector<ValueHandle> findAllReferencesFrom(SEditorObjectSet const& objects);
	std::vector<ValueHandle> findAllReferences(Project const &project);
	std::vector<ValueHandle> findAllReferences(const SEditorObject& object);
	std::vector<SEditorObject> findAllUnreferencedObjects(Project const& project, std::function<bool(SEditorObject)> predicate = nullptr);
	std::vector<SEditorObject> findAllValidReferenceTargets(Project const& project, const ValueHandle& handle );

	SEditorObject findById(const Project& project, const std::string& id);
	SEditorObject findById(const std::vector<SEditorObject>& objects, const std::string& id);
	SEditorObject findByName(const std::vector<SEditorObject>& objects, const std::string& name);

	ValueHandle findByIdAndPath(const Project& project, const std::string& object_id, const std::string& path);

	bool canPasteIntoObject(Project const& project, SEditorObject const& object);
	bool canPasteObjectAsExternalReference(const SEditorObject& editorObject, bool wasTopLevelObjectInSourceProject);
	bool canDeleteUnreferencedResources(const Project& project);
	bool canDuplicateObjects(const std::vector<SEditorObject>& objects, const Project& project);

	SEditorObjectSet collectAllChildren(std::vector<SEditorObject> baseObjects);

	std::vector<SEditorObject> filterForNotResource(const std::vector<SEditorObject>& objects);
	std::vector<SEditorObject> filterByTypeName(const std::vector<SEditorObject>& objects, const std::vector<std::string>& typeNames);
	std::vector<SEditorObject> filterForTopLevelObjectsByTypeName(const std::vector<SEditorObject>& objects, const std::vector<std::string>& typeNames);
	std::vector<SEditorObject> filterForVisibleTopLevelObjects(const std::vector<SEditorObject>& objects);
	
	std::vector<SEditorObject> filterForDeleteableObjects(Project const& project, const std::vector<SEditorObject>& objects);
	std::vector<SEditorObject> filterForMoveableScenegraphChildren(Project const& project, const std::vector<SEditorObject>& objects, SEditorObject const& newParent);

	template<class UserType>
	std::vector<std::shared_ptr<UserType>> filterByType(const std::vector<SEditorObject>& objects) {
		std::vector<std::shared_ptr<UserType>> r;
		for (auto const& o : objects) {
			if (&o->getTypeDescription() == &UserType::typeDescription) {
				r.push_back(o->as<UserType>());
			}
		}
		return r;
	}

	bool isResource(const SEditorObject& object);
	bool isNotResource(const SEditorObject& object);
	bool isProjectSettings(const SEditorObject& object);
	bool isChildHandle(const ValueHandle& handle);
	bool isChildObject(const SEditorObject& child, const SEditorObject& parent);

	/**
	 * @brief Build path description for an object composed of the object names starting at the scenegraph root and descending the scenegraph to the object. The components are separated by '/' but without a leading separator at the beginning.
	 * @param obj arbitrary object.
	 * @return object hierarchy path.
	*/
	std::string getFullObjectHierarchyPath(SEditorObject obj);

	// Determines if the property value (for linkState = false) or the link state (for linkState = true)
	// is changeable in the data model.
	bool isReadOnly(const Project& project, const ValueHandle& handle, bool linkState = false);

	bool isHidden(const Project& project, const ValueHandle& handle);


	// Determines whether an object is read-only content of a prefab instance
	bool isReadOnly(SEditorObject editorObj);

	SLink getLink(const Project& project, const PropertyDescriptor& property);


	enum class CurrentLinkState {
		NOT_LINKED,
		LINKED,
		PARENT_LINKED,
		BROKEN
	};

	// examples of member variable values for various scenarios
	//
	// normal objects except prefab instance subtree and external references
	// no link target    -> (not linked,    ro=???,   target=false)
	// parent is linked  -> (parent linked, ro=true,  target=true)
	// linkable          -> (not linked,    ro=false, target=true)
	// linked            -> (linked,        ro=false, target=true)
	// broken            -> (broken,        ro=false, target=*)
	//
	// states for prefab instance subtree and external reference objects
	// locked            -> (*,             ro=true,  target=*)
	// locked is the state of prefab instance subtree or external reference objects
	struct LinkState {
		CurrentLinkState current;

		// link state read-only: bool flag
		// - indicates if link can be created/removed by command interface
		// - depends on current link state:
		//   link state is read-only if parent is currently linked
		// - depends on read-only state of property or entire object
		//   external reference objects and prefab instance subtree can't be modified, including links
		bool readonly;

		// valid link target: bool flag:
		// - can the property have a link ending on it in principle
		// - controlled only by presence of LinkEndAnnotation:
		//   true even if parent is linked or if link state is read-only
		//   true for external reference or prefab instance subtree objects
		bool validLinkTarget;
	};
	
	CurrentLinkState currentLinkState(const Project& project, const ValueHandle& property);
	LinkState linkState(const Project& project, ValueHandle const& handle);

	// Find all links starting/ending on a given property or any of its child properties.
	std::vector<SLink> getLinksConnectedToPropertySubtree(const Project& project, const ValueHandle& property, bool includeStarting, bool includeEnding);
	
	std::vector<SLink> getLinksConnectedToProperty(const Project& project, const ValueHandle& property, bool includeStarting, bool includeEnding);

	std::vector<SLink> getLinksConnectedToPropertyParents(const Project& project, const ValueHandle& property, bool includeSelf);

	std::vector<SLink> getLinksConnectedToObject(const Project& project, const SEditorObject& object, bool includeStarting, bool includeEnding);

	// Find all links starting or ending on any property of a set of objects.
	// @return The set is partitioned in subsets according to the endpoint object and returned
	//	       as a map indexed by the endpoint object ID.
	template<typename Container>
	std::map<std::string, std::set<SLink>> getLinksConnectedToObjects(const Project& project, const Container& objects, bool includeStarting, bool inlucdeEnding);

	// Find broken links ending on valid properties of the object and return 
	// a formatted error message, showing each broken link.
	// Returns empty string if no broken links found.
	std::string getBrokenLinksErrorMessage(const Project& project, SEditorObject obj);

	// Get all properties that are allowed as the start of a link ending on the given end property.
	// Includes type compatibility checks and loop detection.
	std::set<ValueHandle> allowedLinkStartProperties(const Project& project, const ValueHandle& end);
	/** Get all properties that are allowed as the start of a link ending on the given end property.
	 * Includes type compatibility checks.
	 * @returns pair of [ValueHandle] and bool inidicator if a loop will be created.
	 */
	std::set<std::pair<ValueHandle, bool>> allLinkStartProperties(const Project& project, const ValueHandle& end);

	bool linkSatisfiesConstraints(const PropertyDescriptor& start, const PropertyDescriptor& end);

	// Check if a property is allowed as endpoint of a link.
	bool isValidLinkEnd(const ValueHandle& endProperty);
	// Check if a property is allowed as endpoint of a link.
	bool isValidLinkStart(const ValueHandle& startProperty);

	// Check if two properties can be linked together
	bool userCanCreateLink(const Project& project, const ValueHandle& start, const ValueHandle& link);

	// Check if a link is allowed to exist in the data model.
	bool linkWouldBeAllowed(const Project& project, const PropertyDescriptor& start, const PropertyDescriptor& end);

	// Check if a link between properties would be valid.
	bool linkWouldBeValid(const Project& project, const PropertyDescriptor& start, const PropertyDescriptor& end);

};

}
