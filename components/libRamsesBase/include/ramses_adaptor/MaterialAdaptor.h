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

#include "components/DataChangeDispatcher.h"
#include "ramses_adaptor/ObjectAdaptor.h"
#include "user_types/Material.h"
#include <array>

namespace raco::ramses_adaptor {

class SceneAdaptor;

class MaterialAdaptor final : public TypedObjectAdaptor<user_types::Material, ramses::Effect>, public ILogicPropertyProvider {
private:
	static ramses_base::RamsesEffect createEffect(SceneAdaptor* buildContext);

public:
	explicit MaterialAdaptor(SceneAdaptor* buildContext, user_types::SMaterial material);
	bool isValid();

	bool sync(core::Errors* errors) override;

	ramses_base::RamsesAppearance appearance() {
		return appearance_;
	}

	void getLogicNodes(std::vector<ramses::LogicNode*>& logicNodes) const override;
	ramses::Property* getProperty(const std::vector<std::string_view>& propertyNamesVector) override;
	void onRuntimeError(core::Errors& errors, std::string const& message, core::ErrorLevel level) override;
	std::vector<ExportInformation> getExportInformation() const override;

private:
	ramses_base::RamsesAppearance appearance_;
	ramses_base::RamsesAppearanceBinding appearanceBinding_;

	components::Subscription subscription_;
	components::Subscription optionsSubscription_;
	components::Subscription uniformSubscription_;
};

void updateAppearance(core::Errors* errors, SceneAdaptor* sceneAdaptor, ramses_base::RamsesAppearance appearance, user_types::BlendOptions& options, const core::ValueHandle& optionsContHandle, const core::ValueHandle& uniformConHandle);

};	// namespace raco::ramses_adaptor
