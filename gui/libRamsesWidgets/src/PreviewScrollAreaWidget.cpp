/*
 * SPDX-License-Identifier: MPL-2.0
 *
 * This file is part of Ramses Composer
 * (see https://github.com/bmwcarit/ramses-composer).
 *
 * This Source Code Form is subject to the terms of the Mozilla Public License, v. 2.0.
 * If a copy of the MPL was not distributed with this file, You can obtain one at http://mozilla.org/MPL/2.0/.
 */
#include "ramses_widgets/PreviewScrollAreaWidget.h"

#include <QMouseEvent>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QWheelEvent>
#include <QWidget>
#include <cmath>

namespace raco::ramses_widgets {

PreviewScrollAreaWidget::PreviewScrollAreaWidget(const QSize& sceneSize, QWidget* parent)
	: QAbstractScrollArea{parent}, sceneSize_{sceneSize} {
	connect(horizontalScrollBar(), &QScrollBar::valueChanged, this, &PreviewScrollAreaWidget::updateViewport);
	connect(verticalScrollBar(), &QScrollBar::valueChanged, this, &PreviewScrollAreaWidget::updateViewport);
}

void PreviewScrollAreaWidget::setAutoSizing(const AutoSizing mode) {
	if (sizeMode_ != mode) {
		sizeMode_ = mode;
		updateViewport();
	}
}

void PreviewScrollAreaWidget::autoSizingVerticalFit() {
	setAutoSizing(AutoSizing::VERTICAL_FIT);
}

void PreviewScrollAreaWidget::autoSizingHorizontalFit() {
	setAutoSizing(AutoSizing::HORIZONTAL_FIT);
}

void PreviewScrollAreaWidget::autoSizingBestFit() {
	setAutoSizing(AutoSizing::BEST_FIT);
}

void PreviewScrollAreaWidget::autoSizingOriginalFit() {
	setAutoSizing(AutoSizing::ORIGINAL_FIT);
}

void PreviewScrollAreaWidget::autoSizingOff() {
	setAutoSizing(AutoSizing::OFF);
}
void PreviewScrollAreaWidget::setScaleValue(const double value) {
	if (scaleValue_ != value) {
		scaleValue_ = value;
		updateViewport();
	}
}

void PreviewScrollAreaWidget::resizeEvent(QResizeEvent* /*event*/) {
	updateViewport();
}

std::optional<QPoint> PreviewScrollAreaWidget::globalPositionToPreviewPosition(const QPoint& p) {
	auto localPosition = viewport()->mapFromGlobal(p);
	auto devicePixelScaleFactor = window()->screen()->devicePixelRatio();
	auto result = QPoint{
		static_cast<int>(devicePixelScaleFactor * (horizontalScrollBar()->value() + localPosition.x() - std::max(0, viewportPosition_.x())) / scaleValue_),
		static_cast<int>(sceneSize_.height() - devicePixelScaleFactor * (verticalScrollBar()->value() + localPosition.y() - std::max(0, viewportPosition_.y())) / scaleValue_)};
	if (result.x() >= 0 && result.y() >= 0 && result.x() < sceneSize_.width() && result.y() < sceneSize_.height()) {
		return result;
	} else {
		return {};
	}
}

void PreviewScrollAreaWidget::wheelEvent(QWheelEvent* event) {
#if QT_VERSION < QT_VERSION_CHECK(5, 14, 0)
	mousePivot_ = event->pos();
#else
	mousePivot_ = event->position().toPoint();
#endif
	auto virtSize = scaledSize();
	double centreX = static_cast<double>(horizontalScrollBar()->value() + mousePivot_.x()) / virtSize.width();
	double centerY = static_cast<double>(verticalScrollBar()->value() + mousePivot_.y()) / virtSize.height();
	if (sizeMode_ != AutoSizing::OFF) {
		sizeMode_ = AutoSizing::OFF;
		scaleValue_ = pow(2, ceil(log(scaleValue_) / log(2)));
	}
	if (event->angleDelta().y() > 0) {
		scaleValue_ *= 2.0;
	} else {
		scaleValue_ *= 0.5;
	}
	virtSize = scaledSize();

	updateScrollbarSize(virtSize);
	horizontalScrollBar()->setValue(centreX * virtSize.width() - mousePivot_.x());
	verticalScrollBar()->setValue(centerY * virtSize.height() - mousePivot_.y());

	updateViewport();
}

void PreviewScrollAreaWidget::mousePressEvent(QMouseEvent* event) {
	setCursor(Qt::DragMoveCursor);
	mousePivot_ = event->pos();
}

void PreviewScrollAreaWidget::mouseReleaseEvent(QMouseEvent* event) {
	unsetCursor();
	mousePivot_ = event->pos();
}

void PreviewScrollAreaWidget::mouseMoveEvent(QMouseEvent* event) {
	if (event->MouseButtonPress) {
		const auto delta = event->pos() - mousePivot_;
		horizontalScrollBar()->setValue(horizontalScrollBar()->value() - delta.x());
		verticalScrollBar()->setValue(verticalScrollBar()->value() - delta.y());
		updateViewport();
	}
	mousePivot_ = event->pos();
}

void PreviewScrollAreaWidget::updateViewport() {
	const QSize areaSize = viewport()->size();
	QSize virtualSceneSize_devicePixels;

	auto devicePixelScaleFactor = window()->screen()->devicePixelRatio();

	switch (sizeMode_) {
		case AutoSizing::OFF: {
			virtualSceneSize_devicePixels = scaledSize();
			break;
		}
		case AutoSizing::VERTICAL_FIT: {
			const auto scale = static_cast<float>(areaSize.height() * devicePixelScaleFactor) / sceneSize_.height();
			virtualSceneSize_devicePixels = sceneSize_ * scale;
			scaleValue_ = scale;
			break;
		}
		case AutoSizing::HORIZONTAL_FIT: {
			const auto scale = static_cast<float>(areaSize.width() * devicePixelScaleFactor) / sceneSize_.width();
			virtualSceneSize_devicePixels = sceneSize_ * scale;
			scaleValue_ = scale;
			break;
		}
		case AutoSizing::BEST_FIT: {
			const auto horizontalScale = static_cast<float>(areaSize.width() * devicePixelScaleFactor) / sceneSize_.width();
			const auto verticalScale = static_cast<float>(areaSize.height() * devicePixelScaleFactor) / sceneSize_.height();
			float scale = std::min(horizontalScale, verticalScale);
			virtualSceneSize_devicePixels = sceneSize_ * scale;
			scaleValue_ = scale;
			break;
		}
		case AutoSizing::ORIGINAL_FIT: {
			scaleValue_ = 1;
			virtualSceneSize_devicePixels = scaledSize();
			break;
		}
	}
	QSize virtualSceneSize_virtualPixels = virtualSceneSize_devicePixels / devicePixelScaleFactor;

	updateScrollbarSize(virtualSceneSize_virtualPixels);

	// viewportSize, viewportPosition_, and viewportOffset are given in Qt virtual pixel units
	const QSize viewportSize = virtualSceneSize_virtualPixels.boundedTo(areaSize);
	viewportPosition_ = QPoint{(areaSize.width() - viewportSize.width()) / 2, (areaSize.height() - viewportSize.height()) / 2};
	const QPoint viewportOffset{horizontalScrollBar()->value(), verticalScrollBar()->value()};

	Q_EMIT viewportRectChanged(areaSize, viewportPosition_, viewportOffset, viewportSize, virtualSceneSize_devicePixels, sceneSize_);
	Q_EMIT scaleChanged(scaleValue_);
	Q_EMIT autoSizingChanged(sizeMode_);
}

void PreviewScrollAreaWidget::updateScrollbarSize(const QSize& widgetSize) noexcept {
	const QSize areaSize = viewport()->size();
	verticalScrollBar()->setPageStep(areaSize.height());
	horizontalScrollBar()->setPageStep(areaSize.width());
	verticalScrollBar()->setRange(0, widgetSize.height() - areaSize.height());
	horizontalScrollBar()->setRange(0, widgetSize.width() - areaSize.width());
}

QSize PreviewScrollAreaWidget::scaledSize() const noexcept {
	return sceneSize_ * scaleValue_;
}

void PreviewScrollAreaWidget::setViewport(const QSize& sceneSize) {
	QSize size = sceneSize.boundedTo({4096, 4096}).expandedTo({1, 1});
	if (sceneSize_ != size) {
		sceneSize_ = size;
		updateViewport();
	}
}

}  // namespace raco::ramses_widgets