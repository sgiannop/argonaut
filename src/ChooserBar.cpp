#include "ChooserBar.h"

#include <QCheckBox>
#include <QComboBox>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QVBoxLayout>

ChooserBar::ChooserBar(const ChooserRequest &request, QWidget *parent)
    : QWidget(parent)
{
    setObjectName("chooserBar");
    setAttribute(Qt::WA_StyledBackground);
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(14, 10, 14, 10);
    layout->setSpacing(8);

    using Mode = ChooserRequest::Mode;
    if (request.mode == Mode::Save) {
        auto *row = new QHBoxLayout;
        row->setSpacing(10);
        auto *label = new QLabel(tr("Name"), this);
        label->setObjectName("chooserLabel");
        m_name = new QLineEdit(request.currentName, this);
        m_name->setObjectName("conflictName");
        m_name->setMinimumWidth(320);
        label->setBuddy(m_name);
        connect(m_name, &QLineEdit::returnPressed, this, &ChooserBar::accepted);
        row->addWidget(label);
        row->addWidget(m_name, 1);
        layout->addLayout(row);
    }

    auto *row = new QHBoxLayout;
    row->setSpacing(10);
    for (const ChooserRequest::Choice &choice : request.choices) {
        ChoiceWidget widget;
        widget.id = choice.id;
        if (choice.options.isEmpty()) { // a checkbox
            widget.check = new QCheckBox(choice.label, this);
            widget.check->setObjectName("prefCheck");
            widget.check->setChecked(choice.initial == QLatin1String("true"));
            row->addWidget(widget.check);
        } else {
            auto *label = new QLabel(choice.label, this);
            label->setObjectName("chooserLabel");
            widget.combo = new QComboBox(this);
            widget.combo->setObjectName("propCombo");
            for (const auto &option : choice.options)
                widget.combo->addItem(option.second, option.first);
            const int initial = widget.combo->findData(choice.initial);
            if (initial >= 0)
                widget.combo->setCurrentIndex(initial);
            row->addWidget(label);
            row->addWidget(widget.combo);
        }
        m_choices.append(widget);
    }
    row->addStretch(1);

    if (!request.filters.isEmpty()) {
        m_filter = new QComboBox(this);
        m_filter->setObjectName("propCombo");
        m_filter->setMinimumWidth(180);
        for (const ChooserRequest::Filter &filter : request.filters)
            m_filter->addItem(filter.name);
        m_filter->setCurrentIndex(qMax(0, request.currentFilter));
        connect(m_filter, &QComboBox::currentIndexChanged, this, &ChooserBar::filterChanged);
        row->addWidget(m_filter);
    }

    auto *cancel = new QPushButton(tr("Cancel"), this);
    cancel->setObjectName("conflictButton");
    cancel->setAutoDefault(false);
    connect(cancel, &QPushButton::clicked, this, &ChooserBar::rejected);
    QString accept = request.acceptLabel;
    if (accept.isEmpty())
        accept = request.mode == Mode::Open ? (request.directory ? tr("Select") : tr("Open")) : tr("Save");
    m_accept = new QPushButton(accept, this);
    m_accept->setObjectName("conflictPrimary");
    m_accept->setDefault(true);
    connect(m_accept, &QPushButton::clicked, this, &ChooserBar::accepted);
    row->addWidget(cancel);
    row->addWidget(m_accept);
    layout->addLayout(row);
}

QString ChooserBar::name() const
{
    return m_name ? m_name->text() : QString();
}

void ChooserBar::setName(const QString &name)
{
    if (m_name)
        m_name->setText(name);
}

void ChooserBar::focusName()
{
    if (!m_name)
        return;
    m_name->setFocus();
    const QString text = m_name->text();
    int end = text.lastIndexOf(QLatin1Char('.'));
    if (text.endsWith(QLatin1String(".tar.gz")) || text.endsWith(QLatin1String(".tar.xz"))
        || text.endsWith(QLatin1String(".tar.bz2")))
        end = text.lastIndexOf(QLatin1String(".tar."));
    m_name->setSelection(0, end > 0 ? end : int(text.size()));
}

int ChooserBar::currentFilter() const
{
    return m_filter ? m_filter->currentIndex() : -1;
}

QList<QPair<QString, QString>> ChooserBar::choiceValues() const
{
    QList<QPair<QString, QString>> values;
    for (const ChoiceWidget &widget : m_choices) {
        if (widget.check)
            values.append({widget.id, widget.check->isChecked() ? QStringLiteral("true") : QStringLiteral("false")});
        else if (widget.combo)
            values.append({widget.id, widget.combo->currentData().toString()});
    }
    return values;
}

void ChooserBar::setAcceptEnabled(bool enabled)
{
    m_accept->setEnabled(enabled);
}
