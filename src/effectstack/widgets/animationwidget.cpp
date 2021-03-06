/***************************************************************************
 *   Copyright (C) 2016 by Jean-Baptiste Mardelle (jb@kdenlive.org)        *
 *   This file is part of Kdenlive. See www.kdenlive.org.                  *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or         *
 *   modify it under the terms of the GNU General Public License as        *
 *   published by the Free Software Foundation; either version 2 of        *
 *   the License or (at your option) version 3 or any later version        *
 *   accepted by the membership of KDE e.V. (or its successor approved     *
 *   by the membership of KDE e.V.), which shall act as a proxy            *
 *   defined in Section 14 of version 3 of the license.                    *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program.  If not, see <http://www.gnu.org/licenses/>. *
 ***************************************************************************/

#include "utils/KoIconUtils.h"

#include <QDebug>
#include <QVBoxLayout>
#include <QToolBar>
#include <QDomElement>
#include <QMenu>
#include <QStandardPaths>
#include <QComboBox>
#include <QDir>
#include <QInputDialog>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QMimeData>

#include <KDualAction>
#include <KConfig>
#include <KConfigGroup>
#include <KSelectAction>
#include <klocalizedstring.h>

#include "mlt++/MltAnimation.h"

#include "animationwidget.h"
#include "doubleparameterwidget.h"
#include "monitor/monitor.h"
#include "timeline/keyframeview.h"
#include "timecodedisplay.h"
#include "kdenlivesettings.h"
#include "effectstack/parametercontainer.h"
#include "effectstack/dragvalue.h"
#include "../animkeyframeruler.h"


AnimationWidget::AnimationWidget(EffectMetaInfo *info, int clipPos, int min, int max, int effectIn, const QString &effectId, QDomElement xml, QWidget *parent) :
    QWidget(parent)
    , m_monitor(info->monitor)
    , m_timePos(new TimecodeDisplay(info->monitor->timecode(), this))
    , m_active(false)
    , m_clipPos(clipPos)
    , m_inPoint(min)
    , m_outPoint(max)
    , m_editedKeyframe(-1)
    , m_attachedToEnd(-2)
    , m_xml(xml)
    , m_effectId(effectId)
    , m_spinX(NULL)
    , m_spinY(NULL)
    , m_spinWidth(NULL)
    , m_spinHeight(NULL)
    , m_offset(effectIn - min)
{
    setAcceptDrops(true);
    QVBoxLayout* vbox2 = new QVBoxLayout(this);

    // Keyframe ruler
    m_ruler = new AnimKeyframeRuler(min, max, this);
    connect(m_ruler, SIGNAL(addKeyframe(int)), this, SLOT(slotAddKeyframe(int)));
    connect(m_ruler, SIGNAL(removeKeyframe(int)), this, SLOT(slotDeleteKeyframe(int)));
    vbox2->addWidget(m_ruler);
    vbox2->setContentsMargins(0, 0, 0, 0);
    QToolBar *tb = new QToolBar(this);
    vbox2->addWidget(tb);
    setLayout(vbox2);
    connect(m_ruler, &AnimKeyframeRuler::requestSeek, this, &AnimationWidget::seekToPos);
    connect(m_ruler, &AnimKeyframeRuler::moveKeyframe, this, &AnimationWidget::moveKeyframe);
    connect(m_timePos, SIGNAL(timeCodeEditingFinished()), this, SLOT(slotPositionChanged()));

    // seek to previous
    tb->addAction(KoIconUtils::themedIcon(QStringLiteral("media-skip-backward")), i18n("Previous keyframe"), this, SLOT(slotPrevious()));

    // Add/remove keyframe
    m_addKeyframe = new KDualAction(i18n("Add keyframe"), i18n("Remove keyframe"), this);
    m_addKeyframe->setInactiveIcon(KoIconUtils::themedIcon(QStringLiteral("list-add")));
    m_addKeyframe->setActiveIcon(KoIconUtils::themedIcon(QStringLiteral("list-remove")));
    connect(m_addKeyframe, SIGNAL(activeChangedByUser(bool)), this, SLOT(slotAddDeleteKeyframe(bool)));
    tb->addAction(m_addKeyframe);

    // seek to previous
    tb->addAction(KoIconUtils::themedIcon(QStringLiteral("media-skip-forward")), i18n("Next keyframe"), this, SLOT(slotNext()));

    // Preset combo
    m_presetCombo = new QComboBox(this);
    m_presetCombo->setToolTip(i18n("Presets"));
    connect(m_presetCombo, SIGNAL(currentIndexChanged(int)), this, SLOT(applyPreset(int)));
    tb->addWidget(m_presetCombo);

    // Keyframe type widget
    m_selectType = new KSelectAction(KoIconUtils::themedIcon(QStringLiteral("keyframes")), i18n("Keyframe interpolation"), this);
    QAction *discrete = new QAction(KoIconUtils::themedIcon(QStringLiteral("discrete")), i18n("Discrete"), this);
    discrete->setData((int) mlt_keyframe_discrete);
    discrete->setCheckable(true);
    m_selectType->addAction(discrete);
    QAction *linear = new QAction(KoIconUtils::themedIcon(QStringLiteral("linear")), i18n("Linear"), this);
    linear->setData((int) mlt_keyframe_linear);
    linear->setCheckable(true);
    m_selectType->addAction(linear);
    QAction *curve = new QAction(KoIconUtils::themedIcon(QStringLiteral("smooth")), i18n("Smooth"), this);
    curve->setData((int) mlt_keyframe_smooth);
    curve->setCheckable(true);
    m_selectType->addAction(curve);
    m_selectType->setCurrentAction(linear);
    connect(m_selectType, SIGNAL(triggered(QAction*)), this, SLOT(slotEditKeyframeType(QAction*)));

    KSelectAction *defaultInterp = new KSelectAction(KoIconUtils::themedIcon(QStringLiteral("keyframes")), i18n("Default interpolation"), this);
    discrete = new QAction(KoIconUtils::themedIcon(QStringLiteral("discrete")), i18n("Discrete"), this);
    discrete->setData((int) mlt_keyframe_discrete);
    discrete->setCheckable(true);
    defaultInterp->addAction(discrete);
    linear = new QAction(KoIconUtils::themedIcon(QStringLiteral("linear")), i18n("Linear"), this);
    linear->setData((int) mlt_keyframe_linear);
    linear->setCheckable(true);
    defaultInterp->addAction(linear);
    curve = new QAction(KoIconUtils::themedIcon(QStringLiteral("smooth")), i18n("Smooth"), this);
    curve->setData((int) mlt_keyframe_smooth);
    curve->setCheckable(true);
    defaultInterp->addAction(curve);
    switch(KdenliveSettings::defaultkeyframeinterp()) {
        case mlt_keyframe_discrete:
            defaultInterp->setCurrentAction(discrete);
            break;
        case mlt_keyframe_smooth:
            defaultInterp->setCurrentAction(curve);
            break;
        default:
            defaultInterp->setCurrentAction(linear);
            break;
    }
    connect(defaultInterp, SIGNAL(triggered(QAction*)), this, SLOT(slotSetDefaultInterp(QAction*)));
    m_selectType->setToolBarMode(KSelectAction::MenuMode);

    m_endAttach = new QAction(i18n("Attach keyframe to end"), this);
    m_endAttach->setCheckable(true);
    connect(m_endAttach, &QAction::toggled, this, &AnimationWidget::slotReverseKeyframeType);

    // save preset
    QAction *savePreset = new QAction(KoIconUtils::themedIcon(QStringLiteral("document-save")), i18n("Save preset"), this);
    connect(savePreset, &QAction::triggered, this, &AnimationWidget::savePreset);

    // delete preset
    QAction *delPreset = new QAction(KoIconUtils::themedIcon(QStringLiteral("edit-delete")), i18n("Delete preset"), this);
    connect(delPreset, &QAction::triggered, this, &AnimationWidget::deletePreset);

    QMenu *container = new QMenu;
    tb->addAction(m_selectType);
    container->addAction(m_endAttach);
    container->addAction(savePreset);
    container->addAction(delPreset);
    container->addAction(defaultInterp);

    QToolButton *menuButton = new QToolButton;
    menuButton->setIcon(KoIconUtils::themedIcon(QStringLiteral("kdenlive-menu")));
    menuButton->setToolTip(i18n("Options"));
    menuButton->setMenu(container);
    menuButton->setPopupMode(QToolButton::InstantPopup);
    tb->addWidget(menuButton);

    // Spacer
    QWidget* empty = new QWidget();
    empty->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Preferred);
    tb->addWidget(empty);

    // Timecode
    tb->addWidget(m_timePos);
    m_timePos->setFrame(false);
    m_timePos->setRange(0, m_outPoint - m_inPoint);

    // Prepare property
    mlt_profile profile = m_monitor->profile()->get_profile();
    m_animProperties.set("_profile", profile, 0, NULL, NULL);

    // Display keyframe parameter
    addParameter(xml);
}

AnimationWidget::~AnimationWidget()
{
}

void AnimationWidget::finishSetup()
{
    // Load effect presets
    loadPresets();

    // If only one animated parametes, hide radiobutton
    if (m_doubleWidgets.count() == 1) {
        m_doubleWidgets.first()->hideRadioButton();
    }
    // Load keyframes
    rebuildKeyframes();
}

//static
QString AnimationWidget::getDefaultKeyframes(const QString &defaultValue, bool linearOnly)
{
    QString keyframes = QStringLiteral("0");
    if (linearOnly) {
        keyframes.append(QStringLiteral("="));
    } else {
        switch (KdenliveSettings::defaultkeyframeinterp()) {
            case mlt_keyframe_discrete:
                keyframes.append(QStringLiteral("|="));
                break;
            case mlt_keyframe_smooth:
                keyframes.append(QStringLiteral("~="));
                break;
            default:
                keyframes.append(QStringLiteral("="));
                break;
        }
    }
    keyframes.append(defaultValue);
    return keyframes;
}

void AnimationWidget::updateTimecodeFormat()
{
    m_timePos->slotUpdateTimeCodeFormat();
}

void AnimationWidget::slotPrevious()
{
    int previous = qMax(-m_offset, m_animController.previous_key(m_timePos->getValue() - m_offset - 1)) + m_offset;
    m_ruler->setActiveKeyframe(previous);
    slotPositionChanged(previous, true);
}

void AnimationWidget::slotNext()
{
    int next = m_animController.next_key(m_timePos->getValue() - m_offset + 1) + m_offset;
    if (!m_animController.is_key(next)) {
        // No keyframe after current pos, return end position
        next = m_timePos->maximum();
    } else {
        m_ruler->setActiveKeyframe(next);
    }
    slotPositionChanged(next, true);
}

void AnimationWidget::slotAddKeyframe(int pos, QString paramName, bool directUpdate)
{
    if (paramName.isEmpty()) {
        paramName = m_inTimeline;
    }
    if (pos == -1) {
        pos = m_timePos->getValue();
    }
    pos -= m_offset;
    // Try to get previous key's type
    mlt_keyframe_type type = (mlt_keyframe_type) KdenliveSettings::defaultkeyframeinterp();
    int previous = m_animController.previous_key(pos);
    if (m_animController.is_key(previous)) {
        type =  m_animController.keyframe_type(previous);
    } else {
        int next = m_animController.next_key(pos);
        if (m_animController.is_key(next)) {
            type =  m_animController.keyframe_type(next);
        }
    }

    if (paramName == m_rectParameter) {
        mlt_rect rect = m_animProperties.anim_get_rect(paramName.toUtf8().constData(), pos, m_outPoint);
        m_animProperties.anim_set(paramName.toUtf8().constData(), rect, pos, m_outPoint, type);
    } else {
        double val = m_animProperties.anim_get_double(paramName.toUtf8().constData(), pos, m_outPoint);
        m_animProperties.anim_set(paramName.toUtf8().constData(), val, pos, m_outPoint, type);
    }
    slotPositionChanged(-1, false);
    if (directUpdate) {
        m_ruler->setActiveKeyframe(pos);
        rebuildKeyframes();
        emit parameterChanged();
    }
}

void AnimationWidget::slotDeleteKeyframe(int pos, bool directUpdate)
{
    if (pos == -1) {
        pos = m_timePos->getValue();
    }
    pos -= m_offset;
    m_animController.remove(pos);
    m_selectType->setEnabled(false);
    m_addKeyframe->setActive(false);
    slotPositionChanged(-1, false);
    if (directUpdate) {
        rebuildKeyframes();
        emit parameterChanged();
    }
}

void AnimationWidget::slotAddDeleteKeyframe(bool add)
{
    int pos = m_timePos->getValue();
    QStringList paramNames = m_doubleWidgets.keys();
    if (!add) {
        // Delete keyframe at current pos
        slotDeleteKeyframe(pos, false);
        /*
        // Delete keyframe in all animations at current pos
        for (int i = 0; i < paramNames.count(); i++) {
            m_animController = m_animProperties.get_animation(paramNames.at(i).toUtf8().constData());
            if (m_animController.is_key(pos)) {
                slotDeleteKeyframe(pos, false);
            }
        }*/
    } else {
        // Create keyframe
        slotAddKeyframe(pos, QString(), false);
        /*
        // Add keyframe in all animations
        for (int i = 0; i < paramNames.count(); i++) {
            m_animController = m_animProperties.get_animation(paramNames.at(i).toUtf8().constData());
            if (!m_animController.is_key(pos)) {
                slotAddKeyframe(pos, paramNames.at(i), false);
            }
        }*/
        m_ruler->setActiveKeyframe(pos);
    }
    // Restore default controller
    //m_animController = m_animProperties.get_animation(m_inTimeline.toUtf8().constData());
    // Rebuild
    rebuildKeyframes();
    emit parameterChanged();
}

void AnimationWidget::slotSyncPosition(int relTimelinePos)
{
    // do only sync if this effect is keyframable
    if (m_timePos->maximum() > 0) {
        relTimelinePos = qBound(0, relTimelinePos, m_timePos->maximum());
        slotPositionChanged(relTimelinePos, false);
    }
}

void AnimationWidget::moveKeyframe(int oldPos, int newPos)
{
    bool isKey;
    mlt_keyframe_type type;
    qDebug()<<" * * *MVD KFR: "<<oldPos<<" to "<<newPos<<", OFF: "<<m_offset;
    if (m_animController.get_item(oldPos - m_offset, isKey, type)) {
        qDebug()<<"////////ERROR NO KFR";
        return;
    }
    if (m_inTimeline == m_rectParameter) {
        mlt_rect rect = m_animProperties.anim_get_rect(m_inTimeline.toUtf8().constData(), oldPos - m_offset, m_outPoint);
        m_animController.remove(oldPos - m_offset);
        m_animProperties.anim_set(m_inTimeline.toUtf8().constData(), rect, newPos - m_offset, m_outPoint, type);
    } else {
        double val = m_animProperties.anim_get_double(m_inTimeline.toUtf8().constData(), oldPos - m_offset, m_outPoint);
        m_animController.remove(oldPos - m_offset);
        m_animProperties.anim_set(m_inTimeline.toUtf8().constData(), val, newPos - m_offset, m_outPoint, type);
    }
    /* Move keyframe in all geometries
    QStringList paramNames = m_doubleWidgets.keys();
    for (int i = 0; i < paramNames.count(); i++) {
        double val = m_animProperties.anim_get_double(paramNames.at(i).toUtf8().constData(), oldPos, m_timePos->maximum());
        m_animController = m_animProperties.get_animation(paramNames.at(i).toUtf8().constData());
        m_animController.remove(oldPos);
        m_animProperties.anim_set(paramNames.at(i).toUtf8().constData(), val, newPos, m_timePos->maximum(), type);
    }
    // Restore default controller
    m_animController = m_animProperties.get_animation(m_inTimeline.toUtf8().constData());
    */
    m_ruler->setActiveKeyframe(newPos);
    if (m_attachedToEnd == oldPos) {
        m_attachedToEnd = newPos;
    }
    rebuildKeyframes();
    slotPositionChanged(m_ruler->position(), false);
    emit parameterChanged();
}

void AnimationWidget::rebuildKeyframes()
{
    // Fetch keyframes
    QVector <int> keyframes;
    QVector <int> types;
    int frame;
    mlt_keyframe_type type;
    for (int i = 0; i < m_animController.key_count(); i++) {
        if (!m_animController.key_get(i, frame, type)) {
            frame += m_offset;
            if (frame >= 0) {
		  keyframes << frame;
		  types << (int) type;
	    }
        }
    }
    m_ruler->updateKeyframes(keyframes, types, m_attachedToEnd);
}

void AnimationWidget::updateToolbar()
{
    int pos = m_timePos->getValue();
    QMapIterator<QString, DoubleParameterWidget *> i(m_doubleWidgets);
    while (i.hasNext()) {
        i.next();
        double val = m_animProperties.anim_get_double(i.key().toUtf8().constData(), pos, m_outPoint);
        i.value()->setValue(val * i.value()->factor);
    }
    if (m_animController.is_key(pos)) {
        QList<QAction *> types = m_selectType->actions();
        for (int i = 0; i < types.count(); i++) {
            if (types.at(i)->data().toInt() == (int) m_animController.keyframe_type(pos)) {
                m_selectType->setCurrentAction(types.at(i));
                break;
            }
        }
        m_selectType->setEnabled(true);
        m_addKeyframe->setActive(true);
        if (m_doubleWidgets.value(m_inTimeline))
            m_doubleWidgets.value(m_inTimeline)->enableEdit(true);
    } else {
        m_selectType->setEnabled(false);
        m_addKeyframe->setActive(false);
        if (m_doubleWidgets.value(m_inTimeline))
            m_doubleWidgets.value(m_inTimeline)->enableEdit(false);
    }
}

void AnimationWidget::slotPositionChanged(int pos, bool seek)
{
    if (pos == -1)
        pos = m_timePos->getValue();
    else
        m_timePos->setValue(pos);
    m_ruler->setValue(pos);
    if (m_spinX) {
        updateRect(pos);
    }
    updateSlider(pos - m_offset);
    if (seek)
        emit seekToPos(pos);
}

void AnimationWidget::updateSlider(int pos)
{
    m_endAttach->blockSignals(true);
    QMapIterator<QString, DoubleParameterWidget *> i(m_doubleWidgets);
    while (i.hasNext()) {
        i.next();
        m_animController = m_animProperties.get_animation(i.key().toUtf8().constData());
        double val = m_animProperties.anim_get_double(i.key().toUtf8().constData(), pos, m_outPoint);
        if (!m_animController.is_key(pos)) {
            // no keyframe
            if (m_animController.key_count() <= 1) {
                // Special case: only one keyframe, allow adjusting whatever the position is
                i.value()->enableEdit(true);
                if (!i.value()->hasEditFocus()) {
                    i.value()->setValue(val * i.value()->factor);
                }
                if (i.key() == m_inTimeline) {
                    if (m_active) m_monitor->setEffectKeyframe(true);
                    m_endAttach->setEnabled(true);
                    m_endAttach->setChecked(m_attachedToEnd > -2 && m_animController.key_get_frame(0) >= m_attachedToEnd);
                }
            } else {
                i.value()->enableEdit(false);
                i.value()->setValue(val * i.value()->factor);
                if (i.key() == m_inTimeline) {
                    if (m_active) m_monitor->setEffectKeyframe(false);
                    m_endAttach->setEnabled(false);
                }
            }
            if (i.key() == m_inTimeline) {
                m_selectType->setEnabled(false);
                m_addKeyframe->setActive(false);
            }
        } else {
            // keyframe
            i.value()->setValue(val * i.value()->factor);
            if (i.key() == m_inTimeline) {
                if (m_active) m_monitor->setEffectKeyframe(true);
                m_addKeyframe->setActive(true);
                m_selectType->setEnabled(true);
                m_endAttach->setEnabled(true);
                m_endAttach->setChecked(m_attachedToEnd > -2 && pos >= m_attachedToEnd);
                mlt_keyframe_type currentType = m_animController.keyframe_type(pos);
                QList<QAction *> types = m_selectType->actions();
                for (int i = 0; i < types.count(); i++) {
                    if ((mlt_keyframe_type) types.at(i)->data().toInt() == currentType) {
                        m_selectType->setCurrentAction(types.at(i));
                        break;
                    }
                }
            }
            i.value()->enableEdit(true);
        }
    }
    m_endAttach->blockSignals(false);
    // Restore default controller
    m_animController = m_animProperties.get_animation(m_inTimeline.toUtf8().constData());
}

void AnimationWidget::updateRect(int pos)
{
    m_endAttach->blockSignals(true);
    m_animController = m_animProperties.get_animation(m_rectParameter.toUtf8().constData());
    mlt_rect rect = m_animProperties.anim_get_rect(m_rectParameter.toUtf8().constData(), pos, m_outPoint);
    m_spinX->blockSignals(true);
    m_spinY->blockSignals(true);
    m_spinWidth->blockSignals(true);
    m_spinHeight->blockSignals(true);
    m_spinX->setValue(rect.x);
    m_spinY->setValue(rect.y);
    m_spinWidth->setValue(rect.w);
    m_spinHeight->setValue(rect.h);
    bool enableEdit = false;
    if (!m_animController.is_key(pos)) {
        // no keyframe
        if (m_animController.key_count() <= 1) {
            // Special case: only one keyframe, allow adjusting whatever the position is
            enableEdit = true;
            if (m_active) {
                m_monitor->setEffectKeyframe(true);
            }
            m_endAttach->setEnabled(true);
            m_endAttach->setChecked(m_attachedToEnd > -2 && m_animController.key_get_frame(0) >= m_attachedToEnd);
        } else {
            enableEdit = false;
            if (m_active) {
                m_monitor->setEffectKeyframe(false);
            }
            m_endAttach->setEnabled(false);
        }
        m_selectType->setEnabled(false);
        m_addKeyframe->setActive(false);
    } else {
        // keyframe
        enableEdit = true;
        if (m_active) {
            m_monitor->setEffectKeyframe(true);
        }
        m_addKeyframe->setActive(true);
        m_selectType->setEnabled(true);
        m_endAttach->setEnabled(true);
        m_endAttach->setChecked(m_attachedToEnd > -2 && pos >= m_attachedToEnd);
        mlt_keyframe_type currentType = m_animController.keyframe_type(pos);
        QList<QAction *> types = m_selectType->actions();
        for (int i = 0; i < types.count(); i++) {
            if ((mlt_keyframe_type) types.at(i)->data().toInt() == currentType) {
                m_selectType->setCurrentAction(types.at(i));
                break;
            }
        }
    }
    m_spinX->setEnabled(enableEdit);
    m_spinY->setEnabled(enableEdit);
    m_spinWidth->setEnabled(enableEdit);
    m_spinHeight->setEnabled(enableEdit);
    m_spinX->blockSignals(false);
    m_spinY->blockSignals(false);
    m_spinWidth->blockSignals(false);
    m_spinHeight->blockSignals(false);
    m_endAttach->blockSignals(false);
    setupMonitor(QRect(rect.x, rect.y, rect.w, rect.h));
    // Restore default controller
    m_animController = m_animProperties.get_animation(m_inTimeline.toUtf8().constData());
}

void AnimationWidget::slotEditKeyframeType(QAction *action)
{
    int pos = m_timePos->getValue() - m_offset;
    if (m_animController.is_key(pos)) {
        if (m_rectParameter == m_inTimeline) {
            mlt_rect rect = m_animProperties.anim_get_rect(m_inTimeline.toUtf8().constData(), pos, m_outPoint);
            m_animProperties.anim_set(m_inTimeline.toUtf8().constData(), rect, pos, m_outPoint, (mlt_keyframe_type) action->data().toInt());
        } else {
            double val = m_animProperties.anim_get_double(m_inTimeline.toUtf8().constData(), pos, m_outPoint);
            m_animProperties.anim_set(m_inTimeline.toUtf8().constData(), val, pos, m_outPoint, (mlt_keyframe_type) action->data().toInt());
        }
        /* This is a keyframe, edit for all parameters
        QStringList paramNames = m_doubleWidgets.keys();
        for (int i = 0; i < paramNames.count(); i++) {
            double val = m_animProperties.anim_get_double(paramNames.at(i).toUtf8().constData(), pos, m_timePos->maximum());
            m_animProperties.anim_set(paramNames.at(i).toUtf8().constData(), val, pos, m_timePos->maximum(), (mlt_keyframe_type) action->data().toInt());
        }*/
        rebuildKeyframes();
        setupMonitor();
        emit parameterChanged();
    }
}

void AnimationWidget::slotSetDefaultInterp(QAction *action)
{
    KdenliveSettings::setDefaultkeyframeinterp(action->data().toInt());
}

void AnimationWidget::addParameter(const QDomElement &e)
{
    // Anim properties might at some point require some more infos like profile
    QString keyframes;
    if (e.hasAttribute(QStringLiteral("value"))) {
        keyframes = e.attribute(QStringLiteral("value"));
    } else {
        keyframes = getDefaultKeyframes(e.attribute(QStringLiteral("default")));
    }
    QString paramTag = e.attribute(QStringLiteral("name"));
    m_animProperties.set(paramTag.toUtf8().constData(), keyframes.toUtf8().constData());
    m_attachedToEnd = KeyframeView::checkNegatives(keyframes, m_outPoint);
    m_params.append(e.cloneNode().toElement());
    const QString paramType = e.attribute(QStringLiteral("type"));
    if (paramType == QLatin1String("animated")) {
        // one dimension parameter
        // Required to initialize anim property
        m_animProperties.anim_get_int(paramTag.toUtf8().constData(), 0, m_outPoint);
        buildSliderWidget(paramTag, e);
    } else if (paramType == QLatin1String("animatedrect")) {
        // one dimension parameter
        // TODO: multiple rect parameters in effect ?
        m_rectParameter = paramTag;
        m_inTimeline = paramTag;
        // Required to initialize anim property
        m_animProperties.anim_get_rect(paramTag.toUtf8().constData(), 0, m_outPoint);
        buildRectWidget(paramTag, e);
    }
}

void AnimationWidget::buildSliderWidget(const QString &paramTag, const QDomElement &e)
{
    QLocale locale;
    QDomElement na = e.firstChildElement(QStringLiteral("name"));
    QString paramName = i18n(na.text().toUtf8().data());
    QDomElement commentElem = e.firstChildElement(QStringLiteral("comment"));
    QString comment;
    if (!commentElem.isNull())
        comment = i18n(commentElem.text().toUtf8().data());

    int index = m_params.count() - 1;

    double factor = e.hasAttribute(QStringLiteral("factor")) ? locale.toDouble(e.attribute(QStringLiteral("factor"))) : 1;
    DoubleParameterWidget *doubleparam = new DoubleParameterWidget(paramName, 0,
                                                                   e.attribute(QStringLiteral("min")).toDouble(), e.attribute(QStringLiteral("max")).toDouble(),
                                                                   e.attribute(QStringLiteral("default")).toDouble() * factor, comment, index, e.attribute(QStringLiteral("suffix")), e.attribute(QStringLiteral("decimals")).toInt(), true, this);
    doubleparam->setObjectName(paramTag);
    doubleparam->factor = factor;
    connect(doubleparam, SIGNAL(valueChanged(double)), this, SLOT(slotAdjustKeyframeValue(double)));
    layout()->addWidget(doubleparam);
    if (!e.hasAttribute(QStringLiteral("intimeline")) || e.attribute(QStringLiteral("intimeline")) == QLatin1String("1")) {
        doubleparam->setInTimelineProperty(true);
        doubleparam->setChecked(true);
        m_inTimeline = paramTag;
        m_animController = m_animProperties.get_animation(m_inTimeline.toUtf8().constData());
    }
    m_doubleWidgets.insert(paramTag, doubleparam);
    connect(doubleparam, SIGNAL(displayInTimeline(bool)), this, SLOT(slotUpdateVisibleParameter(bool)));
}

void AnimationWidget::buildRectWidget(const QString &paramTag, const QDomElement &e)
{
    QLocale locale;
    QDomElement na = e.firstChildElement(QStringLiteral("name"));
    QString paramName = i18n(na.text().toUtf8().data());
    QDomElement commentElem = e.firstChildElement(QStringLiteral("comment"));
    QString comment;
    if (!commentElem.isNull())
        comment = i18n(commentElem.text().toUtf8().data());

    QHBoxLayout *horLayout = new QHBoxLayout;
    m_spinX = new DragValue(i18nc("x axis position", "X"), 0, 0, -99000, 99000, -1, QString(), false, this);
    connect(m_spinX, SIGNAL(valueChanged(double)), this, SLOT(slotAdjustRectKeyframeValue()));
    horLayout->addWidget(m_spinX);

    m_spinY = new DragValue(i18nc("y axis position", "Y"), 0, 0, -99000, 99000, -1, QString(), false, this);
    connect(m_spinY, SIGNAL(valueChanged(double)), this, SLOT(slotAdjustRectKeyframeValue()));
    horLayout->addWidget(m_spinY);

    m_spinWidth = new DragValue(i18nc("Frame width", "W"), m_monitor->render->frameRenderWidth(), 0, 1, 99000, -1, QString(), false, this);
    connect(m_spinWidth, SIGNAL(valueChanged(double)), this, SLOT(slotAdjustRectKeyframeValue()));
    horLayout->addWidget(m_spinWidth);

    m_spinHeight = new DragValue(i18nc("Frame height", "H"), m_monitor->render->renderHeight(), 0, 1, 99000, -1, QString(), false, this);
    connect(m_spinHeight, SIGNAL(valueChanged(double)), this, SLOT(slotAdjustRectKeyframeValue()));
    horLayout->addWidget(m_spinHeight);
    horLayout->addStretch(10);

    static_cast<QVBoxLayout *>(layout())->addLayout(horLayout);
    m_animController = m_animProperties.get_animation(paramTag.toUtf8().constData());
}

void AnimationWidget::slotUpdateVisibleParameter(bool display)
{
    if (!display)
        return;
    DoubleParameterWidget *slider = qobject_cast<DoubleParameterWidget *>(QObject::sender());
    if (slider) {
        if (slider->objectName() == m_inTimeline) return;
        if (m_doubleWidgets.value(m_inTimeline))
            m_doubleWidgets.value(m_inTimeline)->setChecked(false);
        m_inTimeline = slider->objectName();
        m_animController = m_animProperties.get_animation(m_inTimeline.toUtf8().constData());
        rebuildKeyframes();
        emit parameterChanged();
    }
}

void AnimationWidget::slotAdjustKeyframeValue(double value)
{
    DoubleParameterWidget *slider = qobject_cast<DoubleParameterWidget *>(QObject::sender());
    if (!slider) return;
    m_inTimeline = slider->objectName();
    m_animController = m_animProperties.get_animation(m_inTimeline.toUtf8().constData());

    int pos = m_ruler->position() - m_offset;
    if (m_animController.is_key(pos)) {
        // This is a keyframe
        m_animProperties.anim_set(m_inTimeline.toUtf8().constData(), value / slider->factor, pos, m_outPoint, (mlt_keyframe_type) m_selectType->currentAction()->data().toInt());
	emit parameterChanged();
    } else if (m_animController.key_count() <= 1) {
	  pos = m_animController.key_get_frame(0);
	  if (pos >= 0) {
	      m_animProperties.anim_set(m_inTimeline.toUtf8().constData(), value / slider->factor, pos, m_outPoint, (mlt_keyframe_type) m_selectType->currentAction()->data().toInt());
	      emit parameterChanged();
	  }
    }
}

void AnimationWidget::slotAdjustRectKeyframeValue()
{
    m_animController = m_animProperties.get_animation(m_rectParameter.toUtf8().constData());
    int pos = m_ruler->position();
    mlt_rect rect;
    rect.x = m_spinX->value();
    rect.y = m_spinY->value();
    rect.w = m_spinWidth->value();
    rect.h = m_spinHeight->value();
    rect.o = 1;
    if (m_animController.is_key(pos)) {
        // This is a keyframe
        m_animProperties.anim_set(m_rectParameter.toUtf8().constData(), rect, pos, m_outPoint, (mlt_keyframe_type) m_selectType->currentAction()->data().toInt());
	emit parameterChanged();
    } else if (m_animController.key_count() <= 1) {
	  pos = m_animController.key_get_frame(0);
	  if (pos >= 0) {
	      m_animProperties.anim_set(m_rectParameter.toUtf8().constData(), rect, pos, m_outPoint, (mlt_keyframe_type) m_selectType->currentAction()->data().toInt());
	      emit parameterChanged();
	  }
    }
}

bool AnimationWidget::isActive(const QString &name) const
{
    return name == m_inTimeline;
}

const QMap <QString, QString> AnimationWidget::getAnimation()
{
    QMap <QString, QString> animationResults;

    if (m_spinX) {
        m_animController = m_animProperties.get_animation(m_rectParameter.toUtf8().constData());
        // TODO: keyframes attached to end
        animationResults.insert(m_rectParameter, m_animController.serialize_cut());
    }

    QMapIterator<QString, DoubleParameterWidget *> i(m_doubleWidgets);
    while (i.hasNext()) {
        i.next();
        m_animController = m_animProperties.get_animation(i.key().toUtf8().constData());
        // no negative keyframe trick, return simple serialize
        if (m_attachedToEnd == -2) {
            animationResults.insert(i.key(), m_animController.serialize_cut());
        } else {
            // Do processing ourselves to include negative values for keyframes relative to end
            int pos;
            mlt_keyframe_type type;
            QString key;
            QLocale locale;
            QStringList result;
            int duration = m_outPoint;
            for(int j = 0; j < m_animController.key_count(); ++j) {
                m_animController.key_get(j, pos, type);
                double val = m_animProperties.anim_get_double(i.key().toUtf8().constData(), pos, duration);
                if (pos >= m_attachedToEnd) {
                    pos = qMin(pos - duration, -1);
                }
                key = QString::number(pos);
                switch (type) {
                    case mlt_keyframe_discrete:
                        key.append("|=");
                        break;
                    case mlt_keyframe_smooth:
                        key.append("~=");
                        break;
                    default:
                        key.append("=");
                        break;
                }
                key.append(locale.toString(val));
                result << key;
            }
            animationResults.insert(i.key(), result.join(";"));
        }
    }
    // restore original controller
    m_animController = m_animProperties.get_animation(m_inTimeline.toUtf8().constData());
    return animationResults;
}

void AnimationWidget::slotReverseKeyframeType(bool reverse)
{
    int pos = m_timePos->getValue();
    if (m_animController.is_key(pos)) {
        if (reverse) {
            m_attachedToEnd = pos;
        } else {
            m_attachedToEnd = -2;
        }
        rebuildKeyframes();
        emit parameterChanged();
    }
}

void AnimationWidget::loadPresets(QString currentText)
{
    m_presetCombo->blockSignals(true);
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + QStringLiteral("/effects/presets/"));
    if (currentText.isEmpty()) currentText = m_presetCombo->currentText();
    while (m_presetCombo->count() > 0) {
        m_presetCombo->removeItem(0);
    }
    m_presetCombo->removeItem(0);
    QMap <QString, QVariant> defaultEntry;
    QStringList paramNames = m_doubleWidgets.keys();
    for (int i = 0; i < paramNames.count(); i++) {
        defaultEntry.insert(paramNames.at(i), getDefaultKeyframes(m_params.at(i).attribute(QStringLiteral("default"))));
    }
    m_presetCombo->addItem(i18n("Default"), defaultEntry);
    loadPreset(dir.absoluteFilePath(m_xml.attribute(QStringLiteral("type"))));
    loadPreset(dir.absoluteFilePath(m_effectId));
    if (!currentText.isEmpty()) {
        int ix = m_presetCombo->findText(currentText);
        if (ix >= 0) m_presetCombo->setCurrentIndex(ix);
    }
    m_presetCombo->blockSignals(false);
}

void AnimationWidget::loadPreset(const QString &path)
{
    KConfig confFile(path, KConfig::SimpleConfig);
    QStringList groups = confFile.groupList();
    foreach(const QString &grp, groups) {
        QMap <QString, QString> entries = KConfigGroup(&confFile, grp).entryMap();
        QMap <QString, QVariant> variantEntries;
        QMapIterator<QString, QString> i(entries);
        while (i.hasNext()) {
            i.next();
            variantEntries.insert(i.key(), i.value());
        }
        m_presetCombo->addItem(grp, variantEntries);
    }
}

void AnimationWidget::applyPreset(int ix)
{
    QMap<QString, QVariant> entries = m_presetCombo->itemData(ix).toMap();
    QStringList presetNames = entries.keys();
    QStringList paramNames = m_doubleWidgets.keys();
    for (int i = 0; i < paramNames.count() && i < presetNames.count(); i++) {
        QString keyframes = entries.value(presetNames.at(i)).toString();
        m_animProperties.set(paramNames.at(i).toUtf8().constData(), keyframes.toUtf8().constData());
    }
    // Required to initialize anim property
    m_animProperties.anim_get_int(m_inTimeline.toUtf8().constData(), 0);
    m_animController = m_animProperties.get_animation(m_inTimeline.toUtf8().constData());
    rebuildKeyframes();
    emit parameterChanged();
}

void AnimationWidget::savePreset()
{
    QDialog d(this);
    QDialogButtonBox *buttonBox = new QDialogButtonBox(QDialogButtonBox::Cancel | QDialogButtonBox::Save);
    QVBoxLayout *l = new QVBoxLayout;
    d.setLayout(l);
    QLineEdit effectName(&d);
    effectName.setPlaceholderText(i18n("Enter preset name"));
    QCheckBox cb(i18n("Save as global preset (available to all effects)"), &d);
    l->addWidget(&effectName);
    l->addWidget(&cb);
    l->addWidget(buttonBox);
    d.connect(buttonBox, SIGNAL(rejected()), &d, SLOT(reject()));
    d.connect(buttonBox, SIGNAL(accepted()), &d, SLOT(accept()));
    if (d.exec() != QDialog::Accepted) {
        return;
    }
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + QStringLiteral("/effects/presets/"));
    if (!dir.exists()) {
        dir.mkpath(QStringLiteral("."));
    }
    QString fileName = cb.isChecked() ? dir.absoluteFilePath(m_xml.attribute(QStringLiteral("type"))) : m_effectId;

    KConfig confFile(dir.absoluteFilePath(fileName), KConfig::SimpleConfig);
    KConfigGroup grp(&confFile, effectName.text());
    // Parse keyframes
    QMap <QString, QString> currentKeyframes = getAnimation();
    QMapIterator<QString, QString> i(currentKeyframes);
    while (i.hasNext()) {
        i.next();
        grp.writeEntry(i.key(), i.value());
    }
    confFile.sync();
    loadPresets(effectName.text());
}

void AnimationWidget::deletePreset()
{
    QString effectName = m_presetCombo->currentText();
    // try deleting as effect preset first
    QDir dir(QStandardPaths::writableLocation(QStandardPaths::DataLocation) + QStringLiteral("/effects/presets/"));
    KConfig confFile(dir.absoluteFilePath(m_effectId), KConfig::SimpleConfig);
    KConfigGroup grp(&confFile, effectName);
    if (grp.exists()) {
    } else {
        // try global preset
        grp = KConfigGroup(&confFile, m_xml.attribute(QStringLiteral("type")));
    }
    grp.deleteGroup();
    confFile.sync();
    loadPresets();
}

void AnimationWidget::setActiveKeyframe(int frame)
{
    m_ruler->setActiveKeyframe(frame);
}

void AnimationWidget::slotUpdateGeometryRect(const QRect r)
{
    int pos = m_timePos->getValue();
    if (!m_animController.is_key(pos)) {
        // no keyframe
        if (m_animController.key_count() <= 1) {
            // Special case: only one keyframe, allow adjusting whatever the position is
            pos = m_animController.key_get_frame(0);
            if (pos < 0) {
                // error
                qDebug()<<"* * *NOT on a keyframe, something is wrong";
                return;
            }
        }
    }
    m_spinX->blockSignals(true);
    m_spinY->blockSignals(true);
    m_spinWidth->blockSignals(true);
    m_spinHeight->blockSignals(true);
    m_spinX->setValue(r.x());
    m_spinY->setValue(r.y());
    m_spinWidth->setValue(r.width());
    m_spinHeight->setValue(r.height());
    m_spinX->blockSignals(false);
    m_spinY->blockSignals(false);
    m_spinWidth->blockSignals(false);
    m_spinHeight->blockSignals(false);
    slotAdjustRectKeyframeValue();
    setupMonitor();
}

void AnimationWidget::slotUpdateCenters(const QVariantList centers)
{
    if (centers.count() != m_animController.key_count()) {
        qDebug()<<"* * * *CENTER POINTS MISMATCH, aborting edit";
    }
    int pos;
    mlt_keyframe_type type;
    for (int i = 0; i < m_animController.key_count(); ++i) {
        m_animController.key_get(i, pos, type);
        mlt_rect rect = m_animProperties.anim_get_rect(m_rectParameter.toUtf8().constData(), pos, m_outPoint);
        // Center rect to new pos
        QPoint offset = centers.at(i).toPoint() - QPoint(rect.x + rect.w / 2, rect.y + rect.h / 2);
        rect.x += offset.x();
        rect.y += offset.y();
        m_animProperties.anim_set(m_rectParameter.toUtf8().constData(), rect, pos, m_outPoint, type);
    }
    slotAdjustRectKeyframeValue();
}

void AnimationWidget::setupMonitor(QRect r)
{
    QVariantList points;
    QVariantList types;
    int pos;
    mlt_keyframe_type type;
    for(int j = 0; j < m_animController.key_count(); ++j) {
        m_animController.key_get(j, pos, type);
        if (m_animController.key_get_type(j) == mlt_keyframe_smooth) {
            types << 1;
        } else {
            types << 0;
        }
        mlt_rect rect = m_animProperties.anim_get_rect(m_rectParameter.toUtf8().constData(), pos, m_outPoint);
        QRect frameRect(rect.x, rect.y, rect.w, rect.h);
        points.append(QVariant(frameRect.center()));
    }
    if (m_active) m_monitor->setUpEffectGeometry(r, points, types);
}

void AnimationWidget::slotSeekToKeyframe(int ix)
{
    int pos = m_animController.key_get_frame(ix);
    slotPositionChanged(pos, true);
}

void AnimationWidget::connectMonitor(bool activate)
{
    m_active = activate;
    if (!m_spinX) {
        // No animated rect displayed in monitor, return
        return;
    }
    if (activate) {
        connect(m_monitor, &Monitor::effectChanged, this, &AnimationWidget::slotUpdateGeometryRect, Qt::UniqueConnection);
        connect(m_monitor, &Monitor::effectPointsChanged, this, &AnimationWidget::slotUpdateCenters, Qt::UniqueConnection);
        connect(m_monitor, SIGNAL(addKeyframe()), this, SLOT(slotAddKeyframe()), Qt::UniqueConnection);
        connect(m_monitor, SIGNAL(seekToKeyframe(int)), this, SLOT(slotSeekToKeyframe(int)), Qt::UniqueConnection);
        connect(m_monitor, &Monitor::seekToNextKeyframe, this, &AnimationWidget::slotNext,Qt::UniqueConnection);
        connect(m_monitor, &Monitor::seekToPreviousKeyframe, this, &AnimationWidget::slotPrevious,Qt::UniqueConnection);
        connect(m_monitor, SIGNAL(addKeyframe()), this, SLOT(slotAddKeyframe()), Qt::UniqueConnection);
        connect(m_monitor, SIGNAL(deleteKeyframe()), this, SLOT(slotDeleteKeyframe()), Qt::UniqueConnection);
        int framePos = qBound<int>(0, m_monitor->render->seekFramePosition() - m_clipPos, m_timePos->maximum());
        slotPositionChanged(framePos, false);
    } else {
        disconnect(m_monitor, &Monitor::effectChanged, this, &AnimationWidget::slotUpdateGeometryRect);
        disconnect(m_monitor, &Monitor::effectPointsChanged, this, &AnimationWidget::slotUpdateCenters);
        disconnect(m_monitor, SIGNAL(addKeyframe()), this, SLOT(slotAddKeyframe()));
        disconnect(m_monitor, SIGNAL(deleteKeyframe()), this, SLOT(slotDeleteKeyframe()));
        disconnect(m_monitor, SIGNAL(addKeyframe()), this, SLOT(slotAddKeyframe()));
        disconnect(m_monitor, &Monitor::seekToNextKeyframe, this, &AnimationWidget::slotNext);
        disconnect(m_monitor, &Monitor::seekToPreviousKeyframe, this, &AnimationWidget::slotPrevious);
        disconnect(m_monitor, SIGNAL(seekToKeyframe(int)), this, SLOT(slotSeekToKeyframe(int)));
    }
}

void AnimationWidget::offsetAnimation(int offset)
{
    int pos = 0;
    mlt_keyframe_type type;
    QString offsetAnimation = QStringLiteral("kdenliveOffset");
    if (m_spinX) {
        m_animController = m_animProperties.get_animation(m_rectParameter.toUtf8().constData());
        for(int j = 0; j < m_animController.key_count(); ++j) {
            m_animController.key_get(j, pos, type);
            mlt_rect rect = m_animProperties.anim_get_rect(m_rectParameter.toUtf8().constData(), pos, m_outPoint);
            m_animProperties.anim_set(offsetAnimation.toUtf8().constData(), rect, pos + offset, m_outPoint, type);
        }
        Mlt::Animation offsetAnim = m_animProperties.get_animation(offsetAnimation.toUtf8().constData());
        m_animProperties.set(m_rectParameter.toUtf8().constData(), offsetAnim.serialize_cut());
        // Required to initialize anim property
        m_animProperties.anim_get_rect(m_rectParameter.toUtf8().constData(), 0, m_outPoint);
        m_animProperties.set(offsetAnimation.toUtf8().constData(), "");
    }

    QMapIterator<QString, DoubleParameterWidget *> i(m_doubleWidgets);
    while (i.hasNext()) {
        i.next();
        m_animController = m_animProperties.get_animation(i.key().toUtf8().constData());
        for(int j = 0; j < m_animController.key_count(); ++j) {
            m_animController.key_get(j, pos, type);
            double val = m_animProperties.anim_get_double(i.key().toUtf8().constData(), pos, m_outPoint);
            m_animProperties.anim_set(offsetAnimation.toUtf8().constData(), val, pos + offset, m_outPoint, type);
        }
        Mlt::Animation offsetAnim = m_animProperties.get_animation(offsetAnimation.toUtf8().constData());
        m_animProperties.set(i.key().toUtf8().constData(), offsetAnim.serialize_cut());
        // Required to initialize anim property
        m_animProperties.anim_get_int(i.key().toUtf8().constData(), 0, m_outPoint);
        m_animProperties.set(offsetAnimation.toUtf8().constData(), "");
    }
    m_offset -= offset;
}

void AnimationWidget::reload(const QString &tag, const QString &data)
{
    m_animProperties.set(tag.toUtf8().constData(), data.toUtf8().constData());
    m_animProperties.anim_get_int(tag.toUtf8().constData(), 0, m_outPoint);
    m_attachedToEnd = KeyframeView::checkNegatives(data, m_outPoint);
    m_inTimeline = tag;
    QMapIterator<QString, DoubleParameterWidget *> i(m_doubleWidgets);
    while (i.hasNext()) {
        i.next();
        i.value()->setChecked(i.key() == tag);
    }
    rebuildKeyframes();
}

