/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This file is part of Ramses Composer
 * (see https://github.com/bmwcarit/ramses-composer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#pragma once

#include "core/UserObjectFactoryInterface.h"

#include "core/Link.h"
#include "user_types/EngineTypeAnnotation.h"

#include <string>
#include <map>
#include <functional>

namespace raco::user_types {

class CubeMap;
using SCubeMap = std::shared_ptr<CubeMap>;
class Texture;
using STexture = std::shared_ptr<Texture>;
class TextureSampler2DBase;
using STextureSampler2DBase = std::shared_ptr<TextureSampler2DBase>;

class BlendOptions;

using namespace core;

template <typename U, typename T>
struct tuple_has_type {};

template <typename U, typename... T>
struct tuple_has_type<U, std::tuple<T...>> : std::disjunction<std::is_same<U, T>...> {
};


class UserObjectFactory : public raco::core::UserObjectFactoryInterface {
public:
	// Master list of all dynamic Property<T, Args...> types that can be deserialized.
	// Dynamic properties are only the ones that can occur in Tables.
	// Value<T> do not appear here, even for pointer types derived from EditorObject.
	using PropertyTypeMapType = std::tuple<
		Property<std::string, URIAnnotation>,
		Property<double, DisplayNameAnnotation, RangeAnnotation<double>>,
		Property<double, DisplayNameAnnotation, RangeAnnotation<double>, LinkEndAnnotation>,

		Property<int, DisplayNameAnnotation, EnumerationAnnotation>,
		Property<bool, DisplayNameAnnotation>,
		Property<Vec4f, DisplayNameAnnotation>,
		Property<BlendOptions, DisplayNameAnnotation>,

		Property<bool, EngineTypeAnnotation>,
		Property<int, EngineTypeAnnotation>,
		Property<int64_t, EngineTypeAnnotation>,
		Property<double, EngineTypeAnnotation>,
		Property<std::string, EngineTypeAnnotation>,
		Property<SEditorObject, EngineTypeAnnotation>,
		Property<Table, EngineTypeAnnotation>,
		Property<Vec2f, EngineTypeAnnotation>,
		Property<Vec3f, EngineTypeAnnotation>,
		Property<Vec4f, EngineTypeAnnotation>,
		Property<Vec2i, EngineTypeAnnotation>,
		Property<Vec3i, EngineTypeAnnotation>,
		Property<Vec4i, EngineTypeAnnotation>,
		Property<STexture, EngineTypeAnnotation>,
		Property<STextureSampler2DBase, EngineTypeAnnotation>,
		Property<SCubeMap, EngineTypeAnnotation>,

		Property<bool, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<int, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<int64_t, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<double, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<std::string, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<SEditorObject, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<Table, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<Vec2f, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<Vec3f, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<Vec4f, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<Vec2i, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<Vec3i, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<Vec4i, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<STexture, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<STextureSampler2DBase, EngineTypeAnnotation, LinkStartAnnotation>,
		Property<SCubeMap, EngineTypeAnnotation, LinkStartAnnotation>,

		Property<bool, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<int, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<int64_t, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<double, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<std::string, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<SEditorObject, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<Table, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<Vec2f, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<Vec3f, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<Vec4f, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<Vec2i, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<Vec3i, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<Vec4i, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<STexture, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<STextureSampler2DBase, EngineTypeAnnotation, LinkEndAnnotation>,
		Property<SCubeMap, EngineTypeAnnotation, LinkEndAnnotation>,
	
		Property<bool, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<int, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<int64_t, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<double, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<std::string, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<Table, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<Vec2f, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<Vec3f, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<Vec4f, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<Vec2i, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<Vec3i, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<Vec4i, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<STextureSampler2DBase, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>,
		Property<SCubeMap, EngineTypeAnnotation, LinkStartAnnotation, LinkEndAnnotation>
		>;

	using StructCreationFunction = std::function<std::shared_ptr<ClassWithReflectedMembers>()>;

	struct StructDescriptor {
		ReflectionInterface::TypeDescriptor description;
		StructCreationFunction createFunc;
	};


	static UserObjectFactory& getInstance();

	virtual SEditorObject createObject(const std::string& type, const std::string& name = std::string(), const std::string& id = std::string()) const override;
	virtual data_storage::ValueBase* createValue(const std::string& type) const override;

	const std::map<std::string, TypeDescriptor>& getTypes() const override;
	bool isUserCreatable(const std::string& type, int featureLevel) const override;
	
	const std::map<std::string, ValueCreationFunction>& getProperties() const;

	const std::map<std::string, StructDescriptor>& getStructTypes() const;

	std::shared_ptr<AnnotationBase> createAnnotation(const std::string& type) const override;

	std::shared_ptr<ClassWithReflectedMembers> createStruct(const std::string& type) const;

	template<typename T, class... Args>
	static ValueBase* staticCreateProperty(T defaultT, Args... params) {
		static_assert(tuple_has_type<Property<T, Args...>, PropertyTypeMapType>::value == true);
		return new Property<T, Args...>(defaultT, params...);
	}

protected:
	template<class T>
	void addType() {
		types_[T::typeDescription.typeName] = {T::typeDescription, createObjectInternal<T>, createValueInternal<T>};
	}

	UserObjectFactory();

private:
	
	using AnnotationCreationFunction = std::function<std::shared_ptr<AnnotationBase>()>;

	struct AnnotationDescriptor {
		ReflectionInterface::TypeDescriptor description;
		AnnotationCreationFunction createFunc;
	};

	template<class T>
	static SEditorObject createObjectInternal(const std::string& name, const std::string& id) {
		return std::make_shared<T>(name, id);
	}

	template <class T>
	static std::shared_ptr<AnnotationBase> createAnnotationInternal();
	template <class T>
	static std::shared_ptr<ClassWithReflectedMembers> createStructInternal();
	template <class T>
	static data_storage::ValueBase* createValueInternal() {
		return new Value<std::shared_ptr<T>>();
	}

	template<class... Args>
	std::map<std::string, UserObjectFactory::TypeDescriptor> makeTypeMap();
	template <class... Args>
	std::map<std::string, ValueCreationFunction> makePropertyMapTuple(std::tuple<Args...>* dummy);
	template <class... Args>
	std::map<std::string, UserObjectFactory::AnnotationDescriptor> makeAnnotationMap();
	template <class... Args>
	std::map<std::string, UserObjectFactory::StructDescriptor> makeStructTypeMap();

	std::map<std::string, TypeDescriptor> types_;
	// Annotations that can be dynmically added to / removed from ClassWithReflectedMembers
	std::map<std::string, AnnotationDescriptor> annotations_;
	std::map<std::string, ValueCreationFunction> properties_;
	std::map<std::string, StructDescriptor> structTypes_;
};

}