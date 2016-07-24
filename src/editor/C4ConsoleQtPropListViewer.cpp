/*
* OpenClonk, http://www.openclonk.org
*
* Copyright (c) 2001-2009, RedWolf Design GmbH, http://www.clonk.de/
* Copyright (c) 2013, The OpenClonk Team and contributors
*
* Distributed under the terms of the ISC license; see accompanying file
* "COPYING" for details.
*
* "Clonk" is a registered trademark of Matthes Bender, used with permission.
* See accompanying file "TRADEMARK" for details.
*
* To redistribute this file separately, substitute the full license texts
* for the above references.
*/

#include "C4Include.h"
#include "script/C4Value.h"
#include "editor/C4ConsoleQtPropListViewer.h"
#include "editor/C4ConsoleQtDefinitionListViewer.h"
#include "editor/C4ConsoleQtState.h"
#include "editor/C4Console.h"
#include "object/C4Object.h"
#include "object/C4GameObjects.h"
#include "object/C4DefList.h"
#include "object/C4Def.h"
#include "script/C4Effect.h"
#include "script/C4AulExec.h"

/* Property path for property setting synchronization */

C4PropertyPath::C4PropertyPath(C4PropList *target) : get_path_type(PPT_Root), set_path_type(PPT_Root)
{
	// Build string to set target: supports objects only
	if (target)
	{
		C4Object *obj = target->GetObject();
		if (obj)
		{
			get_path.Format("Object(%d)", (int)obj->Number);
			root = get_path;
		}
	}
}

C4PropertyPath::C4PropertyPath(C4Effect *fx, C4Object *target_obj) : get_path_type(PPT_Root), set_path_type(PPT_Root)
{
	// Effect property path: Represent as GetEffect("name", Object(%d), index)
	if (!fx || !target_obj) return;
	const char *name = fx->GetName();
	int32_t index = 0;
	for (C4Effect *ofx = target_obj->pEffects; ofx; ofx = ofx->pNext)
		if (ofx == fx) break; else if (!strcmp(ofx->GetName(), name)) ++index;
	get_path.Format("GetEffect(\"%s\", Object(%d), %d)", name, (int)target_obj->Number, (int)index);
	root.Format("Object(%d)", (int)target_obj->Number);
}

C4PropertyPath::C4PropertyPath(const C4PropertyPath &parent, int32_t elem_index) : root(parent.root)
{
	get_path.Format("%s[%d]", parent.GetGetPath(), (int)elem_index);
	get_path_type = set_path_type = PPT_Index;
}

C4PropertyPath::C4PropertyPath(const C4PropertyPath &parent, const char *child_property)
	: get_path_type(PPT_Property), set_path_type(PPT_Property), root(parent.root)
{
	get_path.Format("%s.%s", parent.GetGetPath(), child_property);
}

void C4PropertyPath::SetSetPath(const C4PropertyPath &parent, const char *child_property, C4PropertyPath::PathType path_type)
{
	set_path_type = path_type;
	if (path_type == PPT_Property)
		set_path.Format("%s.%s", parent.GetGetPath(), child_property);
	else if (path_type == PPT_SetFunction)
		set_path.Format("%s->%s", parent.GetGetPath(), child_property);
	else if (path_type == PPT_GlobalSetFunction)
	{
		set_path.Copy(parent.GetGetPath());
		argument.Copy(child_property);
	}
	else if (path_type == PPT_RootSetFunction)
	{
		set_path.Format("%s->%s", parent.GetRoot(), child_property);
	}
	else
	{
		assert(false);
	}
}

void C4PropertyPath::SetProperty(const char *set_string) const
{
	// Compose script to update property
	const char *set_path_c = GetSetPath();
	StdStrBuf script;
	if (set_path_type == PPT_SetFunction || set_path_type == PPT_RootSetFunction)
		script.Format("%s(%s)", set_path_c, set_string);
	else if (set_path_type == PPT_GlobalSetFunction)
		script.Format("%s(%s,%s)", argument.getData(), set_path_c, set_string);
	else
		script.Format("%s=%s", set_path_c, set_string);
	// Execute synced scripted
	::Console.EditCursor.EMControl(CID_Script, new C4ControlScript(script.getData(), 0, false));
}

void C4PropertyPath::SetProperty(const C4Value &to_val) const
{
	SetProperty(to_val.GetDataString(9999999).getData());
}

C4Value C4PropertyPath::ResolveValue() const
{
	if (!get_path.getLength()) return C4VNull;
	return AulExec.DirectExec(::ScriptEngine.GetPropList(), get_path.getData(), "resolve property", false, nullptr);
}

void C4PropertyPath::DoCall(const char *call_string) const
{
	// Compose script call
	StdStrBuf script;
	script.Format(call_string, get_path.getData());
	// Execute synced scripted
	::Console.EditCursor.EMControl(CID_Script, new C4ControlScript(script.getData(), 0, false));
}


/* Property editing */

C4PropertyDelegate::C4PropertyDelegate(const C4PropertyDelegateFactory *factory, C4PropList *props)
	: QObject(), factory(factory), set_function_type(C4PropertyPath::PPT_SetFunction)
{
	// Resolve getter+setter callback names
	if (props)
	{
		creation_props = C4VPropList(props);
		name = props->GetPropertyStr(P_Name);
		set_function = props->GetPropertyStr(P_Set);
		if (props->GetPropertyBool(P_SetGlobal))
		{
			set_function_type = C4PropertyPath::PPT_GlobalSetFunction;
		}
		else if (props->GetPropertyBool(P_SetRoot))
		{
			set_function_type = C4PropertyPath::PPT_RootSetFunction;
		}
		else
		{
			set_function_type = C4PropertyPath::PPT_SetFunction;
		}
		async_get_function = props->GetPropertyStr(P_AsyncGet);
	}
}

void C4PropertyDelegate::UpdateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option) const
{
	editor->setGeometry(option.rect);
}

bool C4PropertyDelegate::GetPropertyValueBase(const C4Value &container, C4String *key, int32_t index, C4Value *out_val) const
{
	switch (container.GetType())
	{
	case C4V_PropList:
		return container._getPropList()->GetPropertyByS(key, out_val);
	case C4V_Array:
		*out_val = container._getArray()->GetItem(index);
		return true;
	default:
		return false;
	}
	
}

bool C4PropertyDelegate::GetPropertyValue(const C4Value &container, C4String *key, int32_t index, C4Value *out_val) const
{
	if (async_get_function)
	{
		C4PropList *props = container.getPropList();
		if (props)
		{
			*out_val = props->Call(async_get_function.Get());
			return true;
		}
		return false;
	}
	else
	{
		return GetPropertyValueBase(container, key, index, out_val);
	}
}

QString C4PropertyDelegate::GetDisplayString(const C4Value &v, C4Object *obj) const
{
	return QString(v.GetDataString().getData());
}

QColor C4PropertyDelegate::GetDisplayTextColor(const C4Value &val, class C4Object *obj) const
{
	return QColor(); // invalid = default
}

QColor C4PropertyDelegate::GetDisplayBackgroundColor(const C4Value &val, class C4Object *obj) const
{
	return QColor(); // invalid = default
}

C4PropertyPath C4PropertyDelegate::GetPathForProperty(C4ConsoleQtPropListModelProperty *editor_prop) const
{
	C4PropertyPath path;
	if (editor_prop->property_path.IsEmpty())
		path = C4PropertyPath(editor_prop->parent_value.getPropList());
	else
		path = editor_prop->property_path;
	return GetPathForProperty(path, editor_prop->key ? editor_prop->key->GetCStr() : nullptr);
}

C4PropertyPath C4PropertyDelegate::GetPathForProperty(const C4PropertyPath &parent_path, const char *default_subpath) const
{
	// Get path
	C4PropertyPath subpath;
	if (default_subpath && *default_subpath)
		subpath = C4PropertyPath(parent_path, default_subpath);
	else
		subpath = parent_path;
	// Set path
	if (GetSetFunction())
	{
		subpath.SetSetPath(parent_path, GetSetFunction(), set_function_type);
	}
	return subpath;
}

C4PropertyDelegateInt::C4PropertyDelegateInt(const C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegate(factory, props), min(std::numeric_limits<int32_t>::min()), max(std::numeric_limits<int32_t>::max()), step(1)
{
	if (props)
	{
		min = props->GetPropertyInt(P_Min, min);
		max = props->GetPropertyInt(P_Max, max);
		step = props->GetPropertyInt(P_Step, step);
	}
}

void C4PropertyDelegateInt::SetEditorData(QWidget *editor, const C4Value &val, const C4PropertyPath &property_path) const
{
	QSpinBox *spinBox = static_cast<QSpinBox*>(editor);
	spinBox->setValue(val.getInt());
}

void C4PropertyDelegateInt::SetModelData(QObject *editor, const C4PropertyPath &property_path, C4ConsoleQtShape *prop_shape) const
{
	QSpinBox *spinBox = static_cast<QSpinBox*>(editor);
	spinBox->interpretText();
	property_path.SetProperty(C4VInt(spinBox->value()));
}

QWidget *C4PropertyDelegateInt::CreateEditor(const C4PropertyDelegateFactory *parent_delegate, QWidget *parent, const QStyleOptionViewItem &option, bool by_selection) const
{
	QSpinBox *editor = new QSpinBox(parent);
	editor->setMinimum(min);
	editor->setMaximum(max);
	editor->setSingleStep(step);
	connect(editor, &QSpinBox::editingFinished, this, [editor, this]() {
		emit EditingDoneSignal(editor);
	});
	return editor;
}

C4PropertyDelegateString::C4PropertyDelegateString(const C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegate(factory, props)
{
}

void C4PropertyDelegateString::SetEditorData(QWidget *editor, const C4Value &val, const C4PropertyPath &property_path) const
{
	Editor *line_edit = static_cast<Editor*>(editor);
	C4String *s = val.getStr();
	line_edit->setText(QString(s ? s->GetCStr() : ""));
}

void C4PropertyDelegateString::SetModelData(QObject *editor, const C4PropertyPath &property_path, C4ConsoleQtShape *prop_shape) const
{
	Editor *line_edit = static_cast<Editor*>(editor);
	// Only set model data when pressing Enter explicitely; not just when leaving 
	if (line_edit->commit_pending)
	{
		QString new_value = line_edit->text();
		// TODO: Would be better to handle escaping in the C4Value-to-string code
		new_value = new_value.replace("\\", "\\\\").replace("\"", "\\\"");
		property_path.SetProperty(C4VString(new_value.toUtf8()));
		line_edit->commit_pending = false;
	}
}

QWidget *C4PropertyDelegateString::CreateEditor(const C4PropertyDelegateFactory *parent_delegate, QWidget *parent, const QStyleOptionViewItem &option, bool by_selection) const
{
	Editor *editor = new Editor(parent);
	// EditingDone only on Return; not just when leaving edit field
	connect(editor, &QLineEdit::returnPressed, editor, [this, editor]() {
		editor->commit_pending = true;
		emit EditingDoneSignal(editor);
	});
	return editor;
}

QString C4PropertyDelegateString::GetDisplayString(const C4Value &v, C4Object *obj) const
{
	// Raw string without ""
	C4String *s = v.getStr();
	return QString(s ? s->GetCStr() : "");
}

C4PropertyDelegateLabelAndButtonWidget::C4PropertyDelegateLabelAndButtonWidget(QWidget *parent)
	: QWidget(parent), layout(nullptr), label(nullptr), button(nullptr), button_pending(false)
{
	layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setMargin(0);
	layout->setSpacing(0);
	label = new QLabel(this);
	QPalette palette = label->palette();
	palette.setColor(label->foregroundRole(), palette.color(QPalette::HighlightedText));
	palette.setColor(label->backgroundRole(), palette.color(QPalette::Highlight));
	label->setPalette(palette);
	layout->addWidget(label);
	button = new QPushButton(QString(LoadResStr("IDS_CNS_MORE")), this);
	layout->addWidget(button);
	// Make sure to draw over view in background
	setPalette(palette);
	setAutoFillBackground(true);
}

C4PropertyDelegateDescendPath::C4PropertyDelegateDescendPath(const class C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegate(factory, props), edit_on_selection(true)
{
	if (props)
	{
		info_proplist = C4VPropList(props); // Descend info is this definition
		edit_on_selection = props->GetPropertyBool(P_EditOnSelection, edit_on_selection);
		descend_path = props->GetPropertyStr(P_DescendPath);
	}
}
 
void C4PropertyDelegateDescendPath::SetEditorData(QWidget *aeditor, const C4Value &val, const C4PropertyPath &property_path) const
{
	Editor *editor = static_cast<Editor *>(aeditor);
	editor->label->setText(GetDisplayString(val, nullptr));
	editor->last_value = val;
	editor->property_path = property_path;
	if (editor->button_pending) emit editor->button->pressed();
}

QWidget *C4PropertyDelegateDescendPath::CreateEditor(const class C4PropertyDelegateFactory *parent_delegate, QWidget *parent, const QStyleOptionViewItem &option, bool by_selection) const
{
	// Otherwise create display and button to descend path
	Editor *editor;
	std::unique_ptr<Editor> peditor((editor = new Editor(parent)));
	connect(editor->button, &QPushButton::pressed, this, [editor, this]() {
		// Value to descend into: Use last value on auto-press because it will not have been updated into the game yet
		// (and cannot be without going async in network mode)
		// On regular button press, re-resolve path to value
		C4Value val = editor->button_pending ? editor->last_value : editor->property_path.ResolveValue();
		bool is_proplist = !!val.getPropList(), is_array = !!val.getArray();
		if (is_proplist || is_array)
		{
			C4PropList *info_proplist = this->info_proplist.getPropList();
			// Allow descending into a sub-path
			C4PropertyPath descend_property_path(editor->property_path);
			if (is_proplist && descend_path)
			{
				// Descend value into sub-path
				val._getPropList()->GetPropertyByS(descend_path.Get(), &val);
				// Descend info_proplist into sub-path
				if (info_proplist)
				{
					C4PropList *info_editorprops = info_proplist->GetPropertyPropList(P_EditorProps);
					if (info_editorprops)
					{
						C4Value sub_info_proplist_val;
						info_editorprops->GetPropertyByS(descend_path.Get(), &sub_info_proplist_val);
						info_proplist = sub_info_proplist_val.getPropList();
					}
				}
				// Descend property path into sub-path
				descend_property_path = C4PropertyPath(descend_property_path, descend_path->GetCStr());
			}
			// No info proplist: Fall back to regular proplist viewing mode
			if (!info_proplist) info_proplist = val.getPropList();
			this->factory->GetPropertyModel()->DescendPath(val, info_proplist, descend_property_path);
			::Console.EditCursor.InvalidateSelection();
		}
	});
	if (by_selection && edit_on_selection) editor->button_pending = true;
	return peditor.release();
}

C4PropertyDelegateArray::C4PropertyDelegateArray(const class C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegateDescendPath(factory, props), max_array_display(0), element_delegate(nullptr)
{
	if (props)
	{
		max_array_display = props->GetPropertyInt(P_Display);
	}
}

QString C4PropertyDelegateArray::GetDisplayString(const C4Value &v, C4Object *obj) const
{
	C4ValueArray *arr = v.getArray();
	if (!arr) return QString(LoadResStr("IDS_CNS_INVALID"));
	int32_t n = v._getArray()->GetSize();
	if (!element_delegate)
	{
		C4Value element_delegate_value;
		C4PropList *info_proplist = this->info_proplist.getPropList();
		if (info_proplist) info_proplist->GetProperty(P_Elements, &element_delegate_value);
		element_delegate = factory->GetDelegateByValue(element_delegate_value);
	}
	if (max_array_display && n)
	{
		QString result = "[";
		for (int32_t i = 0; i < std::min<int32_t>(n, max_array_display); ++i)
		{
			if (i) result += ",";
			result += element_delegate->GetDisplayString(v._getArray()->GetItem(i), obj);
		}
		if (n > max_array_display) result += ",...";
		result += "]";
		return result;
	}
	else
	{
		// Default display (or display with 0 elements): Just show element number
		return QString(LoadResStr("IDS_CNS_ARRAYSHORT")).arg(n);
	}
}

C4PropertyDelegatePropList::C4PropertyDelegatePropList(const class C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegateDescendPath(factory, props)
{
	if (props)
	{
		display_string = props->GetPropertyStr(P_Display);
	}
}


QString C4PropertyDelegatePropList::GetDisplayString(const C4Value &v, C4Object *obj) const
{
	C4PropList *data = v.getPropList();
	if (!data) return QString(LoadResStr("IDS_CNS_INVALID"));
	if (!display_string) return QString("{...}");
	C4PropList *info_proplist = this->info_proplist.getPropList();
	C4PropList *info_editorprops = info_proplist ? info_proplist->GetPropertyPropList(P_EditorProps) : nullptr;
	// Replace all {{name}} by property values of name
	QString result = display_string->GetCStr();
	int32_t pos0, pos1;
	C4Value cv;
	while ((pos0 = result.indexOf("{{")) >= 0)
	{
		pos1 = result.indexOf("}}", pos0+2);
		if (pos1 < 0) break; // placeholder not closed
		// Get child value
		QString substring = result.mid(pos0+2, pos1-pos0-2);
		C4RefCntPointer<C4String> psubstring = ::Strings.RegString(substring.toUtf8());
		if (!data->GetPropertyByS(psubstring.Get(), &cv)) cv.Set0();
		// Try to display using child delegate
		QString display_value;
		if (info_editorprops)
		{
			C4Value child_delegate_val;
			if (info_editorprops->GetPropertyByS(psubstring.Get(), &child_delegate_val))
			{
				C4PropertyDelegate *child_delegate = factory->GetDelegateByValue(child_delegate_val);
				if (child_delegate)
				{
					display_value = child_delegate->GetDisplayString(cv, obj);
				}
			}
		}
		// If there is no child delegate, fall back to GetDataString()
		if (display_value.isEmpty()) display_value = cv.GetDataString().getData();
		// Put value into display string
		result.replace(pos0, pos1 - pos0 + 2, display_value);
	}
	return result;
}


C4PropertyDelegateColor::C4PropertyDelegateColor(const class C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegate(factory, props)
{
}

uint32_t GetTextColorForBackground(uint32_t background_color)
{
	// White text on dark background; black text on bright background
	uint8_t r = (background_color >> 16) & 0xff;
	uint8_t g = (background_color >> 8) & 0xff;
	uint8_t b = (background_color >> 0) & 0xff;
	int32_t lgt = r * 30 + g * 59 + b * 11;
	return (lgt > 16000) ? 0 : 0xffffff;
}

void C4PropertyDelegateColor::SetEditorData(QWidget *aeditor, const C4Value &val, const C4PropertyPath &property_path) const
{
	Editor *editor = static_cast<Editor *>(aeditor);
	uint32_t background_color = static_cast<uint32_t>(val.getInt()) & 0xffffff;
	uint32_t foreground_color = GetTextColorForBackground(background_color);
	QPalette palette = editor->label->palette();
	palette.setColor(editor->label->backgroundRole(), QColor(QRgb(background_color)));
	palette.setColor(editor->label->foregroundRole(), QColor(QRgb(foreground_color)));
	editor->label->setPalette(palette);
	editor->label->setAutoFillBackground(true);
	editor->label->setText(GetDisplayString(val, NULL));
	editor->last_value = val;
}

void C4PropertyDelegateColor::SetModelData(QObject *aeditor, const C4PropertyPath &property_path, C4ConsoleQtShape *prop_shape) const
{
	Editor *editor = static_cast<Editor *>(aeditor);
	property_path.SetProperty(editor->last_value);
}

QWidget *C4PropertyDelegateColor::CreateEditor(const class C4PropertyDelegateFactory *parent_delegate, QWidget *parent, const QStyleOptionViewItem &option, bool by_selection) const
{
	Editor *editor;
	std::unique_ptr<Editor> peditor((editor = new Editor(parent)));
	connect(editor->button, &QPushButton::pressed, this, [editor, this]() {
		QColor clr = QColorDialog::getColor(QColor(editor->last_value.getInt()), editor, QString(), QColorDialog::ShowAlphaChannel);
		editor->last_value.SetInt(clr.rgba());
		this->SetEditorData(editor, editor->last_value, C4PropertyPath()); // force update on display
		emit EditingDoneSignal(editor);
	});
	return peditor.release();
}

QString C4PropertyDelegateColor::GetDisplayString(const C4Value &v, C4Object *obj) const
{
	return QString("#%1").arg(uint32_t(v.getInt()), 8, 16, QChar('0'));
}

QColor C4PropertyDelegateColor::GetDisplayTextColor(const C4Value &val, class C4Object *obj) const
{
	uint32_t background_color = static_cast<uint32_t>(val.getInt()) & 0xffffff;
	uint32_t foreground_color = GetTextColorForBackground(background_color);
	return QColor(foreground_color);
}

QColor C4PropertyDelegateColor::GetDisplayBackgroundColor(const C4Value &val, class C4Object *obj) const
{
	return static_cast<uint32_t>(val.getInt()) & 0xffffff;
}

C4DeepQComboBox::C4DeepQComboBox(QWidget *parent)
	: QComboBox(parent), descending(false), item_clicked(false), last_popup_height(0)
{
	QTreeView *view = new QTreeView(this);
	view->setFrameShape(QFrame::NoFrame);
	view->setSelectionBehavior(QTreeView::SelectRows);
	view->setAllColumnsShowFocus(true);
	view->header()->hide();
	// On expansion, enlarge view if necessery
	connect(view, &QTreeView::expanded, this, [this, view](const QModelIndex &index)
	{
		if (this->model() && view->parentWidget())
		{
			int child_row_count = this->model()->rowCount(index);
			if (child_row_count > 0)
			{
				// Get space to contain expanded leaf+1 item
				QModelIndex last_index = this->model()->index(child_row_count - 1, 0, index);
				int needed_height = view->visualRect(last_index).bottom() - view->visualRect(index).top() + view->height() - view->parentWidget()->height() + view->visualRect(last_index).height();
				int available_height = QApplication::desktop()->availableGeometry(view->mapToGlobal(QPoint(1, 1))).height(); // but do not expand past screen size
				int new_height = std::min(needed_height, available_height - 20);
				if (view->parentWidget()->height() < new_height) view->parentWidget()->resize(view->parentWidget()->width(), (this->last_popup_height=new_height));
			}
		}
	});
	// On selection, highlight object in editor
	view->setMouseTracking(true);
	connect(view, &QTreeView::entered, this, [this](const QModelIndex &index)
	{
		C4Object *obj = nullptr;
		int32_t obj_number = this->model()->data(index, ObjectHighlightRole).toInt();
		if (obj_number) obj = ::Objects.SafeObjectPointer(obj_number);
		::Console.EditCursor.SetHighlightedObject(obj);
	});
	// Connect view to combobox
	setView(view);
	view->viewport()->installEventFilter(this);
}

void C4DeepQComboBox::showPopup()
{
	// New selection: Reset to root of model
	setRootModelIndex(QModelIndex());
	QComboBox::showPopup();
	view()->setMinimumWidth(200); // prevent element list from becoming too small in nested dialogues
	if (last_popup_height && view()->parentWidget()) view()->parentWidget()->resize(view()->parentWidget()->width(), last_popup_height);
}

void C4DeepQComboBox::hidePopup()
{
	QModelIndex current = view()->currentIndex();
	QVariant selected_data = model()->data(current, OptionIndexRole);
	if (item_clicked && (selected_data.type() != QVariant::Int || !descending))
	{
		// Clicked somewhere into the list box: Avoid closing to allow navigation in the tree
		if (descending)
		{
			// Clicked an item text in the tree: Shortcut to opening that tree
			QTreeView *tview = static_cast<QTreeView *>(view());
			if (!tview->isExpanded(current))
			{
				tview->setExpanded(current, true);
				int32_t child_row_count = model()->rowCount(current);
				tview->scrollTo(model()->index(child_row_count - 1, 0, current), QAbstractItemView::EnsureVisible);
				tview->scrollTo(current, QAbstractItemView::EnsureVisible);
			}
		}
	}
	else
	{
		// Otherwise, finish selection
		setRootModelIndex(current.parent());
		setCurrentIndex(current.row());
		::Console.EditCursor.SetHighlightedObject(nullptr);
		QComboBox::hidePopup();
		if (selected_data.type() == QVariant::Int)
		{
			emit NewItemSelected(selected_data.toInt());
		}
	}
	descending = item_clicked = false;
}

void C4DeepQComboBox::setCurrentModelIndex(QModelIndex new_index)
{
	setRootModelIndex(new_index.parent());
	setCurrentIndex(new_index.row());
}

int32_t C4DeepQComboBox::GetCurrentSelectionIndex()
{
	QVariant selected_data = model()->data(model()->index(currentIndex(), 0, rootModelIndex()), OptionIndexRole);
	if (selected_data.type() == QVariant::Int)
	{
		// Valid selection
		return selected_data.toInt();
	}
	else
	{
		// Invalid selection
		return -1;
	}
}

// event filter for view: Catch mouse clicks to prevent closing from simple mouse clicks
bool C4DeepQComboBox::eventFilter(QObject *obj, QEvent *event)
{
	if (obj == view()->viewport() && event->type() == QEvent::MouseButtonPress)
	{
		QPoint pos = static_cast<QMouseEvent *>(event)->pos();
		QModelIndex pressed_index = view()->indexAt(pos);
		item_clicked = true;
		descending = view()->visualRect(pressed_index).contains(pos);
	}
	return false;
}

void C4PropertyDelegateEnumEditor::paintEvent(QPaintEvent *ev)
{
	// Draw self
	QWidget::paintEvent(ev);
	// Draw shape widget
	if (paint_parameter_delegate && parameter_widget)
	{
		QPainter p(this);
		QStyleOptionViewItem view_item;
		view_item.rect.setTopLeft(parameter_widget->mapToParent(parameter_widget->rect().topLeft()));
		view_item.rect.setBottomRight(parameter_widget->mapToParent(parameter_widget->rect().bottomRight()));
		paint_parameter_delegate->Paint(&p, view_item, last_parameter_val);
		//p.fillRect(view_item.rect, QColor("red"));
	}
}

C4PropertyDelegateEnum::C4PropertyDelegateEnum(const C4PropertyDelegateFactory *factory, C4PropList *props, const C4ValueArray *poptions)
	: C4PropertyDelegate(factory, props)
{
	// Build enum options from C4Value definitions in script
	if (!poptions && props) poptions = props->GetPropertyArray(P_Options);
	C4String *default_option_key, *default_value_key = nullptr;
	if (props)
	{
		default_option_key = props->GetPropertyStr(P_OptionKey);
		default_value_key = props->GetPropertyStr(P_ValueKey);
	}
	if (poptions)
	{
		options.reserve(poptions->GetSize());
		for (int32_t i = 0; i < poptions->GetSize(); ++i)
		{
			const C4Value &v = poptions->GetItem(i);
			C4PropList *props = v.getPropList();
			if (!props) continue;
			Option option;
			option.name = props->GetPropertyStr(P_Name);
			if (!option.name.Get()) option.name = ::Strings.RegString("???");
			option.group = props->GetPropertyStr(P_Group);
			option.value_key = props->GetPropertyStr(P_ValueKey);
			if (!option.value_key) option.value_key = default_value_key;
			props->GetProperty(P_Value, &option.value);
			props->GetProperty(P_Get, &option.value_function);
			option.type = C4V_Type(props->GetPropertyInt(P_Type, C4V_Any));
			option.option_key = props->GetPropertyStr(P_OptionKey);
			if (!option.option_key) option.option_key = default_option_key;
			// Derive storage type from given elements in delegate definition
			if (option.type != C4V_Any)
				option.storage_type = Option::StorageByType;
			else if (option.option_key && option.value.GetType() != C4V_Nil)
				option.storage_type = Option::StorageByKey;
			else
				option.storage_type = Option::StorageByValue;
			// Child delegate for value (resolved at runtime because there may be circular references)
			props->GetProperty(P_Delegate, &option.adelegate_val);
			options.push_back(option);
		}
	}
}

QStandardItemModel *C4PropertyDelegateEnum::CreateOptionModel() const
{
	// Create a QStandardItemModel tree from all options and their groups
	std::unique_ptr<QStandardItemModel> model(new QStandardItemModel());
	model->setColumnCount(1);
	int idx = 0;
	for (const Option &opt : options)
	{
		QStandardItem *parent = model->invisibleRootItem();
		if (opt.group)
		{
			QStringList group_names = QString(opt.group->GetCStr()).split(QString("/"));
			for (const QString &group_name : group_names)
			{
				int row_index = -1;
				for (int check_row_index = 0; check_row_index < parent->rowCount(); ++check_row_index)
					if (parent->child(check_row_index, 0)->text() == group_name)
					{
						row_index = check_row_index;
						parent = parent->child(check_row_index, 0);
						break;
					}
				if (row_index < 0)
				{
					QStandardItem *new_group = new QStandardItem(group_name);
					parent->appendRow(new_group);
					parent = new_group;
				}
			}
		}
		QStandardItem *new_item = new QStandardItem(QString(opt.name->GetCStr()));
		new_item->setData(QVariant(idx), C4DeepQComboBox::OptionIndexRole);
		C4Object *item_obj_data = opt.value.getObj();
		if (item_obj_data) new_item->setData(QVariant(item_obj_data->Number), C4DeepQComboBox::ObjectHighlightRole);
		parent->appendRow(new_item);
		++idx;
	}
	return model.release();
}

void C4PropertyDelegateEnum::ClearOptions()
{
	options.clear();
}

void C4PropertyDelegateEnum::ReserveOptions(int32_t num)
{
	options.reserve(num);
}

void C4PropertyDelegateEnum::AddTypeOption(C4String *name, C4V_Type type, const C4Value &val, C4PropertyDelegate *adelegate)
{
	Option option;
	option.name = name;
	option.type = type;
	option.value = val;
	option.storage_type = Option::StorageByType;
	option.adelegate = adelegate;
	options.push_back(option);
}

void C4PropertyDelegateEnum::AddConstOption(C4String *name, const C4Value &val, C4String *group)
{
	Option option;
	option.name = name;
	option.group = group;
	option.value = val;
	option.storage_type = Option::StorageByValue;
	options.push_back(option);
}

int32_t C4PropertyDelegateEnum::GetOptionByValue(const C4Value &val) const
{
	int32_t iopt = 0;
	bool match = false;
	for (auto &option : options)
	{
		switch (option.storage_type)
		{
		case Option::StorageByType:
			match = (val.GetTypeEx() == option.type);
			break;
		case Option::StorageByValue:
			match = (val == option.value);
			break;
		case Option::StorageByKey: // Compare value to value in property. Assume undefined as nil.
		{
			C4PropList *props = val.getPropList();
			C4PropList *def_props = option.value.getPropList();
			if (props && def_props)
			{
				C4Value propval, defval;
				props->GetPropertyByS(option.option_key.Get(), &propval);
				def_props->GetPropertyByS(option.option_key.Get(), &defval);
				match = (defval == propval);
			}
			break;
		}
		default: break;
		}
		if (match) break;
		++iopt;
	}
	// If no option matches, just pick first
	return match ? iopt : -1;
}

void C4PropertyDelegateEnum::UpdateEditorParameter(C4PropertyDelegateEnum::Editor *editor, bool by_selection) const
{
	// Recreate parameter settings editor associated with the currently selected option of an enum
	if (editor->parameter_widget)
	{
		editor->parameter_widget->deleteLater();
		editor->parameter_widget = NULL;
	}
	editor->paint_parameter_delegate = nullptr;
	int32_t idx = editor->option_box->GetCurrentSelectionIndex();
	if (idx < 0 || idx >= options.size()) return;
	const Option &option = options[idx];
	// Lazy-resolve parameter delegate
	EnsureOptionDelegateResolved(option);
	// Create editor if needed
	if (option.adelegate)
	{
		// Determine value to be shown in editor
		C4Value parameter_val;
		if (!by_selection)
		{
			// Showing current selection: From last_val assigned in SetEditorData
			parameter_val = editor->last_val;
		}
		else
		{
			// Selecting a new item: Set the default value
			parameter_val = option.value;
			// Although the default value is taken directly from SetEditorData, it needs to be set here to make child access into proplists and arrays possible
			// (note that actual setting is delayed by control queue and this may often the wrong value in some cases - the correct value will be shown on execution of the queue)
			SetOptionValue(editor->last_get_path, option);
		}
		// Resolve parameter value
		if (option.value_key)
		{
			C4PropList *props = editor->last_val.getPropList();
			if (props) props->GetPropertyByS(option.value_key.Get(), &parameter_val);
		}
		// Show it
		editor->parameter_widget = option.adelegate->CreateEditor(factory, editor, QStyleOptionViewItem(), by_selection);
		if (editor->parameter_widget)
		{
			editor->layout->addWidget(editor->parameter_widget);
			C4PropertyPath delegate_value_path = editor->last_get_path;
			if (option.value_key) delegate_value_path = C4PropertyPath(delegate_value_path, option.value_key->GetCStr());
			option.adelegate->SetEditorData(editor->parameter_widget, parameter_val, delegate_value_path);
			// Forward editing signals
			connect(option.adelegate, &C4PropertyDelegate::EditorValueChangedSignal, editor->parameter_widget, [this, editor](QWidget *changed_editor)
			{
				if (changed_editor == editor->parameter_widget)
					if (!editor->updating)
						emit EditorValueChangedSignal(editor);
			});
			connect(option.adelegate, &C4PropertyDelegate::EditingDoneSignal, editor->parameter_widget, [this, editor](QWidget *changed_editor)
			{
				if (changed_editor == editor->parameter_widget) emit EditingDoneSignal(editor);
			});
		}
		else
		{
			// If the parameter widget is a shape display, show a dummy widget displaying the shape instead
			const C4PropertyDelegateShape *shape_delegate = option.adelegate->GetDirectShapeDelegate();
			if (shape_delegate)
			{
				// dummy widget that is not rendered. shape rendering is forwarded through own paint function
				editor->parameter_widget = new QWidget(editor);
				editor->layout->addWidget(editor->parameter_widget);
				editor->parameter_widget->setAttribute(Qt::WA_NoSystemBackground);
				editor->parameter_widget->setAttribute(Qt::WA_TranslucentBackground);
				editor->parameter_widget->setAttribute(Qt::WA_TransparentForMouseEvents);
				editor->paint_parameter_delegate = shape_delegate;
				editor->last_parameter_val = parameter_val;
			}
		}
	}
}

QModelIndex C4PropertyDelegateEnum::GetModelIndexByID(QStandardItemModel *model, QStandardItem *parent_item, int32_t id, const QModelIndex &parent) const
{
	// Resolve data stored in model to model index in tree
	for (int row = 0; row < parent_item->rowCount(); ++row)
	{
		QStandardItem *child = parent_item->child(row, 0);
		QVariant v = child->data(C4DeepQComboBox::OptionIndexRole);
		if (v.type() == QVariant::Int && v.toInt() == id) return model->index(row, 0, parent);
		if (child->rowCount())
		{
			QModelIndex child_match = GetModelIndexByID(model, child, id, model->index(row, 0, parent));
			if (child_match.isValid()) return child_match;
		}
	}
	return QModelIndex();
}

void C4PropertyDelegateEnum::SetEditorData(QWidget *aeditor, const C4Value &val, const C4PropertyPath &property_path) const
{
	Editor *editor = static_cast<Editor*>(aeditor);
	editor->last_val = val;
	editor->last_get_path = property_path;
	editor->updating = true;
	// Update option selection
	int32_t index = std::max<int32_t>(GetOptionByValue(val), 0);
	QStandardItemModel *model = static_cast<QStandardItemModel *>(editor->option_box->model());
	editor->option_box->setCurrentModelIndex(GetModelIndexByID(model, model->invisibleRootItem(), index, QModelIndex()));
	editor->last_selection_index = index;
	// Update parameter
	UpdateEditorParameter(editor, false);
	editor->updating = false;
}

void C4PropertyDelegateEnum::SetModelData(QObject *aeditor, const C4PropertyPath &property_path, C4ConsoleQtShape *prop_shape) const
{
	// Fetch value from editor
	Editor *editor = static_cast<Editor*>(aeditor);
	QStandardItemModel *model = static_cast<QStandardItemModel *>(editor->option_box->model());
	QModelIndex selected_model_index = model->index(editor->option_box->currentIndex(), 0, editor->option_box->rootModelIndex());
	QVariant vidx = model->data(selected_model_index, C4DeepQComboBox::OptionIndexRole);
	if (vidx.type() != QVariant::Int) return;
	int32_t idx = vidx.toInt();
	if (idx < 0 || idx >= options.size()) return;
	const Option &option = options[idx];
	// Store directly in value or in a proplist field?
	C4PropertyPath use_path;
	if (option.value_key.Get())
		use_path = C4PropertyPath(property_path, option.value_key->GetCStr());
	else
		use_path = property_path;
	// Value from a parameter or directly from the enum?
	if (option.adelegate)
	{
		// Default value on enum change (on main path; not use_path because the default value is always givne as the whole proplist)
		if (editor->option_changed) SetOptionValue(property_path, option);
		// Value from a parameter.
		// Using a setter function?
		use_path = option.adelegate->GetPathForProperty(use_path, nullptr);
		option.adelegate->SetModelData(editor->parameter_widget, use_path, prop_shape);
	}
	else
	{
		// No parameter. Use value.
		SetOptionValue(use_path, option);
	}
	editor->option_changed = false;
}

void C4PropertyDelegateEnum::SetOptionValue(const C4PropertyPath &use_path, const C4PropertyDelegateEnum::Option &option) const
{
	// After an enum entry has been selected, set its value
	// Either directly by value or through a function
	if (option.value_function.GetType() == C4V_Function)
	{
		use_path.SetProperty(FormatString("Call(%s, %s, %s)", option.value_function.GetDataString().getData(), use_path.GetRoot(), option.value.GetDataString(20).getData()).getData());
	}
	else
	{
		use_path.SetProperty(option.value);
	}
}

QWidget *C4PropertyDelegateEnum::CreateEditor(const C4PropertyDelegateFactory *parent_delegate, QWidget *parent, const QStyleOptionViewItem &option, bool by_selection) const
{
	Editor *editor = new Editor(parent);
	editor->layout = new QHBoxLayout(editor);
	editor->layout->setContentsMargins(0, 0, 0, 0);
	editor->layout->setMargin(0);
	editor->layout->setSpacing(0);
	editor->updating = true;
	editor->option_box = new C4DeepQComboBox(editor);
	editor->layout->addWidget(editor->option_box);
	for (auto &option : options) editor->option_box->addItem(option.name->GetCStr());
	connect(editor->option_box, &C4DeepQComboBox::NewItemSelected, editor, [editor, this](int32_t newval) {
		if (!editor->updating) this->UpdateOptionIndex(editor, newval); });
	editor->updating = false;
	editor->option_box->setModel(CreateOptionModel());
	editor->option_box->model()->setParent(editor->option_box);
	return editor;
}

void C4PropertyDelegateEnum::UpdateOptionIndex(C4PropertyDelegateEnum::Editor *editor, int newval) const
{
	// Update value and parameter delegate if selection changed
	if (newval != editor->last_selection_index)
	{
		editor->option_changed = true;
		editor->last_selection_index = newval;
		UpdateEditorParameter(editor, true);
		emit EditorValueChangedSignal(editor);
	}
}

void C4PropertyDelegateEnum::EnsureOptionDelegateResolved(const Option &option) const
{
	// Lazy-resolve parameter delegate
	if (!option.adelegate && option.adelegate_val.GetType() != C4V_Nil)
		option.adelegate = factory->GetDelegateByValue(option.adelegate_val);
}

QString C4PropertyDelegateEnum::GetDisplayString(const C4Value &v, class C4Object *obj) const
{
	// Display string from value
	int32_t idx = GetOptionByValue(v);
	if (idx < 0)
	{
		// Value not found: Default display
		return C4PropertyDelegate::GetDisplayString(v, obj);
	}
	else
	{
		// Value found: Display option string plus parameter
		const Option &option = options[idx];
		QString result = option.name->GetCStr();
		// Lazy-resolve parameter delegate
		EnsureOptionDelegateResolved(option);
		if (option.adelegate)
		{
			C4Value param_val = v;
			if (option.value_key.Get())
			{
				C4PropList *vp = v.getPropList();
				if (vp) vp->GetPropertyByS(option.value_key, &param_val);
			}
			result += " ";
			result += option.adelegate->GetDisplayString(param_val, obj);
		}
		return result;
	}
}

const C4PropertyDelegateShape *C4PropertyDelegateEnum::GetShapeDelegate(C4Value &val, C4PropertyPath *shape_path) const
{
	// Does this delegate own a shape? Forward decision into selected option.
	int32_t option_idx = GetOptionByValue(val);
	if (option_idx < 0) return nullptr;
	const Option &option = options[option_idx];
	EnsureOptionDelegateResolved(option);
	if (!option.adelegate) return nullptr;
	if (option.value_key.Get())
	{
		*shape_path = option.adelegate->GetPathForProperty(*shape_path, option.value_key->GetCStr());
		C4PropList *vp = val.getPropList();
		if (vp) vp->GetPropertyByS(option.value_key, &val);
	}
	return option.adelegate->GetShapeDelegate(val, shape_path);
}

bool C4PropertyDelegateEnum::Paint(QPainter *painter, const QStyleOptionViewItem &option, const C4Value &val) const
{
	// Custom painting: Forward to selected child delegate
	int32_t option_idx = GetOptionByValue(val);
	if (option_idx < 0) return false;
	const Option &selected_option = options[option_idx];
	EnsureOptionDelegateResolved(selected_option);
	if (!selected_option.adelegate) return false;
	if (selected_option.adelegate->HasCustomPaint())
	{
		QStyleOptionViewItem parameter_option = QStyleOptionViewItem(option);
		parameter_option.rect.adjust(parameter_option.rect.width()/2, 0, 0, 0);
		C4Value parameter_val = val;
		if (selected_option.value_key.Get())
		{
			C4PropList *vp = val.getPropList();
			if (vp) vp->GetPropertyByS(selected_option.value_key, &parameter_val);
		}
		selected_option.adelegate->Paint(painter, parameter_option, parameter_val);
	}
	// Always return false to draw self using the standard method
	return false;
}

C4PropertyDelegateDef::C4PropertyDelegateDef(const C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegateEnum(factory, props)
{
	// nil is always an option
	C4String *empty_name = props ? props->GetPropertyStr(P_EmptyName) : nullptr;
	AddConstOption(empty_name ? empty_name : ::Strings.RegString("nil"), C4VNull);
	// Collect sorted definitions
	C4String *filter_property = props ? props->GetPropertyStr(P_Filter) : nullptr;
	if (filter_property)
	{
		// With filter just create a flat list
		std::vector<C4Def *> defs = ::Definitions.GetAllDefs(filter_property);
		std::sort(defs.begin(), defs.end(), [](C4Def *a, C4Def *b) -> bool {
			return strcmp(a->GetName(), b->GetName()) < 0;
		});
		// Add them
		for (C4Def *def : defs)
		{
			C4RefCntPointer<C4String> option_name = ::Strings.RegString(FormatString("%s (%s)", def->id.ToString(), def->GetName()));
			AddConstOption(option_name, C4Value(def), nullptr);
		}
	}
	else
	{
		// Without filter copy tree from definition list model
		C4ConsoleQtDefinitionListModel *def_list_model = factory->GetDefinitionListModel();
		// Recursively add all defs from model
		AddDefinitions(def_list_model, QModelIndex(), nullptr);
	}
}

void C4PropertyDelegateDef::AddDefinitions(C4ConsoleQtDefinitionListModel *def_list_model, QModelIndex parent, C4String *group)
{
	int32_t count = def_list_model->rowCount(parent);
	for (int32_t i = 0; i < count; ++i)
	{
		QModelIndex index = def_list_model->index(i, 0, parent);
		C4Def *def = def_list_model->GetDefByModelIndex(index);
		C4RefCntPointer<C4String> name = ::Strings.RegString(def_list_model->GetNameByModelIndex(index));
		if (def) AddConstOption(name.Get(), C4Value(def), group);
		if (def_list_model->rowCount(index))
		{
			AddDefinitions(def_list_model, index, group ? ::Strings.RegString(FormatString("%s/%s", group->GetCStr(), name->GetCStr()).getData()) : name.Get());
		}
	}
}

C4PropertyDelegateObject::C4PropertyDelegateObject(const C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegateEnum(factory, props), max_nearby_objects(20)
{
	// Settings
	if (props)
	{
		filter = props->GetPropertyStr(P_Filter);
	}
	// Actual object list is created/updated when the editor is created
}

C4RefCntPointer<C4String> C4PropertyDelegateObject::GetObjectEntryString(C4Object *obj) const
{
	// Compose object display string from containment(*), name, position (@x,y) and object number (#n)
	return ::Strings.RegString(FormatString("%s%s @%d,%d (#%d)", obj->Contained ? "*" : "", obj->GetName(), (int)obj->GetX(), (int)obj->GetY(), (int)obj->Number));
}

void C4PropertyDelegateObject::UpdateObjectList()
{
	// Re-create object list from current position
	ClearOptions();
	// Get matching objects first
	std::vector<C4Object *> objects;
	for (C4Object *obj : ::Objects) if (obj->Status)
	{
		C4Value filter_val;
		if (filter)
		{
			if (!obj->GetPropertyByS(filter, &filter_val)) continue;
			if (!filter_val) continue;
		}
		objects.push_back(obj);
	}
	// Get list sorted by distance from selected object
	std::vector<C4Object *> objects_by_distance;
	int32_t cx=0, cy=0;
	if (::Console.EditCursor.GetCurrentSelectionPosition(&cx, &cy))
	{
		objects_by_distance = objects;
		auto ObjDist = [cx, cy](C4Object *o) { return (o->GetX() - cx)*(o->GetX() - cx) + (o->GetY() - cy)*(o->GetY() - cy); };
		std::stable_sort(objects_by_distance.begin(), objects_by_distance.end(), [&ObjDist](C4Object *a, C4Object *b) { return ObjDist(a) < ObjDist(b); });
	}
	size_t num_nearby = objects_by_distance.size();
	bool has_all_objects_list = (num_nearby > max_nearby_objects);
	if (has_all_objects_list) num_nearby = max_nearby_objects;
	// Add actual objects
	ReserveOptions(1 + num_nearby + !!num_nearby + (has_all_objects_list ? objects.size() : 0));
	AddConstOption(::Strings.RegString("nil"), C4VNull); // nil is always an option
	if (num_nearby)
	{
		// TODO: "Select object" entry
		//AddCallbackOption(LoadResStr("IDS_CNS_SELECTOBJECT"));
		// Nearby list
		C4RefCntPointer<C4String> nearby_group;
		// If there are main objects, Create a subgroup. Otherwise, just put all elements into the main group.
		if (has_all_objects_list) nearby_group = ::Strings.RegString(LoadResStr("IDS_CNS_NEARBYOBJECTS"));
		for (int32_t i = 0; i < num_nearby; ++i)
		{
			C4Object *obj = objects_by_distance[i];
			AddConstOption(GetObjectEntryString(obj).Get(), C4VObj(obj), nearby_group.Get());
		}
		// All objects
		if (has_all_objects_list)
		{
			C4RefCntPointer<C4String> all_group = ::Strings.RegString(LoadResStr("IDS_CNS_ALLOBJECTS"));
			for (C4Object *obj : objects) AddConstOption(GetObjectEntryString(obj).Get(), C4VObj(obj), all_group.Get());
		}
	}
}

QWidget *C4PropertyDelegateObject::CreateEditor(const class C4PropertyDelegateFactory *parent_delegate, QWidget *parent, const QStyleOptionViewItem &option, bool by_selection) const
{
	// Update object list for created editor
	// (This should be safe since the object delegate cannot contain nested delegates)
	const_cast<C4PropertyDelegateObject *>(this)->UpdateObjectList();
	return C4PropertyDelegateEnum::CreateEditor(parent_delegate, parent, option, by_selection);
}

QString C4PropertyDelegateObject::GetDisplayString(const C4Value &v, class C4Object *obj) const
{
	C4Object *vobj = v.getObj();
	if (vobj)
	{
		C4RefCntPointer<C4String> s = GetObjectEntryString(vobj);
		return QString(s->GetCStr());
	}
	else
	{
		return QString(v.GetDataString().getData());
	}
}

C4PropertyDelegateBool::C4PropertyDelegateBool(const C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegateEnum(factory, props)
{
	// Add boolean options
	ReserveOptions(2);
	AddConstOption(::Strings.RegString(LoadResStr("IDS_CNS_FALSE")), C4VBool(false));
	AddConstOption(::Strings.RegString(LoadResStr("IDS_CNS_TRUE")), C4VBool(true));
}

bool C4PropertyDelegateBool::GetPropertyValue(const C4Value &container, C4String *key, int32_t index, C4Value *out_val) const
{
	// Force value to bool
	bool success = GetPropertyValueBase(container, key, index, out_val);
	if (out_val->GetType() != C4V_Bool) *out_val = C4VBool(!!*out_val);
	return success;
}

C4PropertyDelegateHasEffect::C4PropertyDelegateHasEffect(const class C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegateBool(factory, props)
{
	if (props) effect = props->GetPropertyStr(P_Effect);
}

bool C4PropertyDelegateHasEffect::GetPropertyValue(const C4Value &container, C4String *key, int32_t index, C4Value *out_val) const
{
	const C4Object *obj = container.getObj();
	if (obj && effect)
	{
		bool has_effect = false;
		for (C4Effect *fx = obj->pEffects; fx; fx = fx->pNext)
			if (!fx->IsDead())
				if (!strcmp(fx->GetName(), effect->GetCStr()))
				{
					has_effect = true;
					break;
				}
		*out_val = C4VBool(has_effect);
		return true;
	}
	return false;
}


C4PropertyDelegateC4ValueEnum::C4PropertyDelegateC4ValueEnum(const C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegateEnum(factory, props)
{
	// Add default C4Value selections
	ReserveOptions(10);
	AddTypeOption(::Strings.RegString("nil"), C4V_Nil, C4VNull);
	AddTypeOption(::Strings.RegString("bool"), C4V_Bool, C4VNull, factory->GetDelegateByValue(C4VString("bool")));
	AddTypeOption(::Strings.RegString("int"), C4V_Int, C4VNull, factory->GetDelegateByValue(C4VString("int")));
	AddTypeOption(::Strings.RegString("string"), C4V_String, C4VNull, factory->GetDelegateByValue(C4VString("string")));
	AddTypeOption(::Strings.RegString("array"), C4V_Array, C4VNull, factory->GetDelegateByValue(C4VString("array")));
	AddTypeOption(::Strings.RegString("function"), C4V_Function, C4VNull, factory->GetDelegateByValue(C4VString("function")));
	AddTypeOption(::Strings.RegString("object"), C4V_Object, C4VNull, factory->GetDelegateByValue(C4VString("object")));
	AddTypeOption(::Strings.RegString("def"), C4V_Def, C4VNull, factory->GetDelegateByValue(C4VString("def")));
	AddTypeOption(::Strings.RegString("effect"), C4V_Effect, C4VNull, factory->GetDelegateByValue(C4VString("effect")));
	AddTypeOption(::Strings.RegString("proplist"), C4V_PropList, C4VNull, factory->GetDelegateByValue(C4VString("proplist")));
}

C4PropertyDelegateC4ValueInputEditor::C4PropertyDelegateC4ValueInputEditor(QWidget *parent)
	: QWidget(parent), layout(NULL), edit(NULL), extended_button(NULL), commit_pending(false)
{
	layout = new QHBoxLayout(this);
	layout->setContentsMargins(0, 0, 0, 0);
	layout->setMargin(0);
	layout->setSpacing(0);
	edit = new QLineEdit(this);
	layout->addWidget(edit);
	extended_button = new QPushButton("...", this);
	extended_button->setMaximumWidth(extended_button->fontMetrics().boundingRect("...").width() + 6);
	layout->addWidget(extended_button);
	extended_button->hide();
	edit->setFocus();
	setLayout(layout);
}

void C4PropertyDelegateC4ValueInput::SetEditorData(QWidget *aeditor, const C4Value &val, const C4PropertyPath &property_path) const
{
	Editor *editor = static_cast<Editor *>(aeditor);
	editor->edit->setText(val.GetDataString().getData());
	if (val.GetType() == C4V_PropList || val.GetType() == C4V_Array)
	{
		editor->extended_button->show();
		editor->property_path = property_path;
	}
	else
	{
		editor->extended_button->hide();
	}
}

void C4PropertyDelegateC4ValueInput::SetModelData(QObject *aeditor, const C4PropertyPath &property_path, C4ConsoleQtShape *prop_shape) const
{
	// Only set model data when pressing Enter explicitely; not just when leaving 
	Editor *editor = static_cast<Editor *>(aeditor);
	if (editor->commit_pending)
	{
		property_path.SetProperty(editor->edit->text().toUtf8());
		editor->commit_pending = false;
	}
}

QWidget *C4PropertyDelegateC4ValueInput::CreateEditor(const class C4PropertyDelegateFactory *parent_delegate, QWidget *parent, const QStyleOptionViewItem &option, bool by_selection) const
{
	// Editor is just an edit box plus a "..." button for array/proplist types
	Editor *editor = new Editor(parent);
	// EditingDone only on Return; not just when leaving edit field
	connect(editor->edit, &QLineEdit::returnPressed, editor, [this, editor]() {
		editor->commit_pending = true;
		emit EditingDoneSignal(editor);
	});
	connect(editor->extended_button, &QPushButton::pressed, editor, [this, editor]() {
		C4Value val = editor->property_path.ResolveValue();
		if (val.getPropList() || val.getArray())
		{
			this->factory->GetPropertyModel()->DescendPath(val, val.getPropList(), editor->property_path);
			::Console.EditCursor.InvalidateSelection();
		}
	});
	return editor;
}


/* Areas shown in viewport */

C4PropertyDelegateShape::C4PropertyDelegateShape(const class C4PropertyDelegateFactory *factory, C4PropList *props)
	: C4PropertyDelegate(factory, props), clr(0xffff0000), can_move_center(false)
{
	if (props)
	{
		shape_type = props->GetPropertyStr(P_Type);
		clr = props->GetPropertyInt(P_Color) | 0xff000000;
		can_move_center = props->GetPropertyBool(P_CanMoveCenter);
	}
}

void C4PropertyDelegateShape::SetModelData(QObject *editor, const C4PropertyPath &property_path, C4ConsoleQtShape *prop_shape) const
{
	if (prop_shape && prop_shape->GetParentDelegate() == this) property_path.SetProperty(prop_shape->GetValue());
}

bool C4PropertyDelegateShape::Paint(QPainter *painter, const QStyleOptionViewItem &option, const C4Value &val) const
{
	// Background color
	if (option.state & QStyle::State_Selected)
		painter->fillRect(option.rect, option.palette.highlight());
	else
		painter->fillRect(option.rect, option.palette.base());
	// Draw a frame in shape color
	painter->save();
	QColor frame_color = QColor(QRgb(clr & 0xffffff));
	int32_t width = Clamp<int32_t>(option.rect.height() / 8, 2, 6) &~1;
	QPen rect_pen(QBrush(frame_color), width, Qt::SolidLine, Qt::SquareCap, Qt::MiterJoin);
	painter->setPen(rect_pen);
	QRect inner_rect = option.rect.adjusted(width / 2, width / 2, -width / 2, -width / 2);
	if (shape_type && shape_type->GetData() == "circle")
	{
		painter->drawEllipse(inner_rect);
		if (can_move_center) painter->drawPoint(inner_rect.center());
	}
	else
	{
		painter->drawRect(inner_rect);
	}
	painter->restore();
	return true;
}


/* Delegate factory: Create delegates based on the C4Value type */

C4PropertyDelegate *C4PropertyDelegateFactory::CreateDelegateByPropList(C4PropList *props) const
{
	if (props)
	{
		const C4String *str = props->GetPropertyStr(P_Type);
		if (str)
		{
			// create default base types
			if (str->GetData() == "int") return new C4PropertyDelegateInt(this, props);
			if (str->GetData() == "string") return new C4PropertyDelegateString(this, props);
			if (str->GetData() == "array") return new C4PropertyDelegateArray(this, props);
			if (str->GetData() == "proplist") return new C4PropertyDelegatePropList(this, props);
			if (str->GetData() == "color") return new C4PropertyDelegateColor(this, props);
			if (str->GetData() == "def") return new C4PropertyDelegateDef(this, props);
			if (str->GetData() == "object") return new C4PropertyDelegateObject(this, props);
			if (str->GetData() == "enum") return new C4PropertyDelegateEnum(this, props);
			if (str->GetData() == "bool") return new C4PropertyDelegateBool(this, props);
			if (str->GetData() == "has_effect") return new C4PropertyDelegateHasEffect(this, props);
			if (str->GetData() == "c4valueenum") return new C4PropertyDelegateC4ValueEnum(this, props);
			if (str->GetData() == "rect" || str->GetData() == "circle") return new C4PropertyDelegateShape(this, props);
			if (str->GetData() == "any") return new C4PropertyDelegateC4ValueInput(this, props);
			// unknown type
			LogF("Invalid delegate type: %s.", str->GetCStr());
		}
	}
	// Default fallback
	return new C4PropertyDelegateC4ValueInput(this, props);
}

C4PropertyDelegate *C4PropertyDelegateFactory::GetDelegateByValue(const C4Value &val) const
{
	auto iter = delegates.find(val.getPropList());
	if (iter != delegates.end()) return iter->second.get();
	C4PropertyDelegate *new_delegate = CreateDelegateByPropList(val.getPropList());
	delegates.insert(std::make_pair(val.getPropList(), std::unique_ptr<C4PropertyDelegate>(new_delegate)));
	return new_delegate;
}

C4PropertyDelegate *C4PropertyDelegateFactory::GetDelegateByIndex(const QModelIndex &index) const
{
	C4ConsoleQtPropListModel::Property *prop = property_model->GetPropByIndex(index);
	if (!prop) return NULL;
	if (!prop->delegate) prop->delegate = GetDelegateByValue(prop->delegate_info);
	return prop->delegate;
}

void C4PropertyDelegateFactory::ClearDelegates()
{
	delegates.clear();
}

void C4PropertyDelegateFactory::EditorValueChanged(QWidget *editor)
{
	emit commitData(editor);
}

void C4PropertyDelegateFactory::EditingDone(QWidget *editor)
{
	emit commitData(editor);
	//emit closeEditor(editor); - done by qt somewhere else...
}

void C4PropertyDelegateFactory::setEditorData(QWidget *editor, const QModelIndex &index) const
{
	// Put property value from proplist into editor
	C4PropertyDelegate *d = GetDelegateByIndex(index);
	if (!CheckCurrentEditor(d, editor)) return;
	// Fetch property only first time - ignore further updates to the same value to simplify editing
	C4ConsoleQtPropListModel::Property *prop = property_model->GetPropByIndex(index);
	if (!prop) return;
	C4Value val;
	d->GetPropertyValue(prop->parent_value, prop->key, index.row(), &val);
	if (!prop->about_to_edit && val == last_edited_value) return;
	last_edited_value = val;
	prop->about_to_edit = false;
	d->SetEditorData(editor, val, d->GetPathForProperty(prop));
}

void C4PropertyDelegateFactory::setModelData(QWidget *editor, QAbstractItemModel *model, const QModelIndex &index) const
{
	// Fetch property value from editor and set it into proplist
	C4PropertyDelegate *d = GetDelegateByIndex(index);
	if (!CheckCurrentEditor(d, editor)) return;
	C4ConsoleQtPropListModel::Property *prop = property_model->GetPropByIndex(index);
	SetPropertyData(d, editor, prop);
}

void C4PropertyDelegateFactory::SetPropertyData(const C4PropertyDelegate *d, QObject *editor, C4ConsoleQtPropListModel::Property *editor_prop) const
{
	// Set according to delegate
	d->SetModelData(editor, d->GetPathForProperty(editor_prop), editor_prop->shape.Get());
}

QWidget *C4PropertyDelegateFactory::createEditor(QWidget *parent, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	C4PropertyDelegate *d = GetDelegateByIndex(index);
	if (!d) return NULL;
	C4ConsoleQtPropListModel::Property *prop = property_model->GetPropByIndex(index);
	prop->about_to_edit = true;
	QWidget *editor = d->CreateEditor(this, parent, option, true);
	// Connect value change signals (if editing is possible for this property)
	// For some reason, commitData needs a non-const pointer
	if (editor)
	{
		connect(d, &C4PropertyDelegate::EditorValueChangedSignal, editor, [editor, this](QWidget *signal_editor) {
			if (signal_editor == editor) const_cast<C4PropertyDelegateFactory *>(this)->EditorValueChanged(editor);
		});
		connect(d, &C4PropertyDelegate::EditingDoneSignal, editor, [editor, this](QWidget *signal_editor) {
			if (signal_editor == editor) const_cast<C4PropertyDelegateFactory *>(this)->EditingDone(editor);
		});
	}
	current_editor = editor;
	current_editor_delegate = d;
	return editor;
}

void C4PropertyDelegateFactory::destroyEditor(QWidget *editor, const QModelIndex &index) const
{
	if (editor == current_editor)
	{
		current_editor = nullptr;
		current_editor_delegate = nullptr;
		::Console.EditCursor.SetHighlightedObject(nullptr);
	}
	QStyledItemDelegate::destroyEditor(editor, index);
}

void C4PropertyDelegateFactory::updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	C4PropertyDelegate *d = GetDelegateByIndex(index);
	if (!CheckCurrentEditor(d, editor)) return;
	return d->UpdateEditorGeometry(editor, option);
}

QSize C4PropertyDelegateFactory::sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	int height = QApplication::fontMetrics().height() + 4;
	return QSize(100, height);
}

void C4PropertyDelegateFactory::paint(QPainter *painter, const QStyleOptionViewItem &option, const QModelIndex &index) const
{
	// Delegate has custom painting?
	C4ConsoleQtPropListModel::Property *prop = property_model->GetPropByIndex(index);
	C4PropertyDelegate *d = GetDelegateByIndex(index);
	if (d && prop && d->HasCustomPaint())
	{
		C4Value val;
		d->GetPropertyValue(prop->parent_value, prop->key, index.row(), &val);
		if (d->Paint(painter, option, val)) return;
	}
	// Otherwise use default paint implementation
	QStyledItemDelegate::paint(painter, option, index);
}

void C4PropertyDelegateFactory::OnPropListChanged()
{
	if (current_editor) emit closeEditor(current_editor);
}

bool C4PropertyDelegateFactory::CheckCurrentEditor(C4PropertyDelegate *d, QWidget *editor) const
{
	if (!d || (editor && editor != current_editor) || d != current_editor_delegate)
	{
		//const_cast<C4PropertyDelegateFactory *>(this)->emit closeEditor(current_editor);
		destroyEditor(current_editor, QModelIndex());
		return false;
	}
	return true;
}


/* Proplist table view */

C4ConsoleQtPropListModel::C4ConsoleQtPropListModel(C4PropertyDelegateFactory *delegate_factory)
	: delegate_factory(delegate_factory), selection_model(nullptr)
{
	header_font.setBold(true);
	connect(this, &C4ConsoleQtPropListModel::ProplistChanged, this, &C4ConsoleQtPropListModel::UpdateSelection, Qt::QueuedConnection);
	layout_valid = false;
}

C4ConsoleQtPropListModel::~C4ConsoleQtPropListModel()
{
}

bool C4ConsoleQtPropListModel::AddPropertyGroup(C4PropList *add_proplist, int32_t group_index, QString name, C4PropList *target_proplist, C4Object *base_effect_object, C4String *default_selection, int32_t *default_selection_index)
{
	auto new_properties = add_proplist->GetSortedLocalProperties(false);
	if (!new_properties.size()) return false;
	if (property_groups.size() == group_index)
	{
		layout_valid = false;
		property_groups.resize(group_index + 1);
	}
	PropertyGroup &properties = property_groups[group_index];
	C4PropListStatic *proplist_static = add_proplist->IsStatic();
	properties.name = name;
	if (properties.props.size() != new_properties.size())
	{
		layout_valid = false;
		properties.props.resize(new_properties.size());
	}
	for (int32_t i = 0; i < new_properties.size(); ++i)
	{
		Property *prop = &properties.props[i];
		// Property access path
		prop->parent_value.SetPropList(target_proplist);
		if (base_effect_object)
			// Access to effect
			prop->property_path = C4PropertyPath(target_proplist->GetEffect(), base_effect_object);
		else
			prop->property_path = target_path;
		// ID for default selection memory
		C4String *prop_id = new_properties[i];
		if (default_selection == prop_id) *default_selection_index = i;
		// Property data
		prop->key = NULL;
		prop->display_name = NULL;
		prop->delegate_info.Set0(); // default C4Value delegate
		prop->group_idx = group_index;
		C4Value published_prop_val;
		add_proplist->GetPropertyByS(new_properties[i], &published_prop_val);
		C4PropList *published_prop = published_prop_val.getPropList();
		if (published_prop)
		{
			prop->key = published_prop->GetPropertyStr(P_Key);
			prop->display_name = published_prop->GetPropertyStr(P_Name);
			prop->delegate_info.SetPropList(published_prop);
		}
		if (!prop->key) properties.props[i].key = prop_id;
		if (!prop->display_name) properties.props[i].display_name = prop_id;
		prop->delegate = delegate_factory->GetDelegateByValue(prop->delegate_info);
		C4Value v;
		C4Value v_target_proplist = C4VPropList(target_proplist);
		prop->delegate->GetPropertyValue(v_target_proplist, prop->key, 0, &v);
		// Connect editable shape to property
		C4PropertyPath new_shape_property_path = prop->delegate->GetPathForProperty(prop);
		const C4PropertyDelegateShape *new_shape_delegate = prop->delegate->GetShapeDelegate(v, &new_shape_property_path);
		if (new_shape_delegate != prop->shape_delegate || !(prop->shape_property_path == new_shape_property_path))
		{
			prop->shape_delegate = new_shape_delegate;
			prop->shape_property_path = new_shape_property_path;
			if (new_shape_delegate)
			{
				C4ConsoleQtShape *shape = ::Console.EditCursor.GetShapes()->CreateShape(base_effect_object ? base_effect_object : target_proplist->GetObject(), new_shape_delegate->GetCreationProps().getPropList(), v, new_shape_delegate);
				C4PropertyDelegateFactory *factory = this->delegate_factory;
				connect(shape, &C4ConsoleQtShape::ShapeDragged, new_shape_delegate, [factory, new_shape_delegate, shape, prop]() {
					new_shape_delegate->SetModelData(nullptr, prop->shape_property_path, shape);
				});
				prop->shape.Set(shape);
			}
			else
			{
				prop->shape.Clear();
			}
		}
	}
	return true;
}

void C4ConsoleQtPropListModel::SetBasePropList(C4PropList *new_proplist)
{
	// Clear stack and select new proplist
	// Update properties
	target_value.SetPropList(new_proplist);
	base_proplist.SetPropList(new_proplist);
	// objects derive their custom properties
	info_proplist.SetPropList(target_value.getObj());
	target_path = C4PropertyPath(new_proplist);
	target_path_stack.clear();
	UpdateValue(true);
	delegate_factory->OnPropListChanged();
}

void C4ConsoleQtPropListModel::DescendPath(const C4Value &new_value, C4PropList *new_info_proplist, const C4PropertyPath &new_path)
{
	// Add previous proplist to stack
	target_path_stack.push_back(TargetStackEntry(target_path, target_value, info_proplist));
	// descend
	target_value = new_value;
	info_proplist.SetPropList(new_info_proplist);
	target_path = new_path;
	UpdateValue(true);
	delegate_factory->OnPropListChanged();
}

void C4ConsoleQtPropListModel::AscendPath()
{
	// Go up in target stack (if possible)
	for (;;)
	{
		if (!target_path_stack.size())
		{
			SetBasePropList(nullptr);
			return;
		}
		TargetStackEntry entry = target_path_stack.back();
		target_path_stack.pop_back();
		if (!entry.value || !entry.info_proplist) continue; // property was removed; go up further in stack
		// Safety: Make sure we're still on the same value
		C4Value target = entry.path.ResolveValue();
		if (!target.IsIdenticalTo(entry.value)) continue;
		// Set new value
		target_path = entry.path;
		target_value = entry.value;
		info_proplist = entry.info_proplist;
		UpdateValue(true);
		break;
	}
	// Any current editor needs to close
	delegate_factory->OnPropListChanged();
}

void C4ConsoleQtPropListModel::UpdateValue(bool select_default)
{
	emit layoutAboutToBeChanged();
	// Update target value from path
	target_value = target_path.ResolveValue();
	// Safe-get from C4Values in case any prop lists or arrays got deleted
	int32_t num_groups, default_selection_group = -1, default_selection_index = -1;
	switch (target_value.GetType())
	{
	case C4V_PropList:
		num_groups = UpdateValuePropList(target_value._getPropList(), &default_selection_group, &default_selection_index);
		break;
	case C4V_Array:
		num_groups = UpdateValueArray(target_value._getArray(), &default_selection_group, &default_selection_index);
		break;
	default: // probably nil
		num_groups = 0;
		break;
	}
	// Update model range
	if (num_groups != property_groups.size())
	{
		layout_valid = false;
		property_groups.resize(num_groups);
	}
	if (!layout_valid)
	{
		// We do not adjust persistent indices for now
		// Usually, if layout changed, it's because the target value changed and we don't want to select/expand random stuff in the new proplist
		layout_valid = true;
	}
	emit layoutChanged();
	QModelIndex topLeft = index(0, 0, QModelIndex());
	QModelIndex bottomRight = index(rowCount() - 1, columnCount() - 1, QModelIndex());
	emit dataChanged(topLeft, bottomRight);
	// Initial selection
	if (select_default) emit ProplistChanged(default_selection_group, default_selection_index);
}

void C4ConsoleQtPropListModel::UpdateSelection(int32_t major_sel, int32_t minor_sel) const
{
	if (selection_model)
	{
		// Select by indexed elements only
		selection_model->clearSelection();
		if (major_sel >= 0)
		{
			QModelIndex sel = index(major_sel, 0, QModelIndex());
			if (minor_sel >= 0) sel = index(minor_sel, 0, sel);
			selection_model->select(sel, QItemSelectionModel::SelectCurrent);
		}
		else
		{
			selection_model->select(QModelIndex(), QItemSelectionModel::SelectCurrent);
		}
	}
}

int32_t C4ConsoleQtPropListModel::UpdateValuePropList(C4PropList *target_proplist, int32_t *default_selection_group, int32_t *default_selection_index)
{
	assert(target_proplist);
	C4PropList *base_proplist = this->base_proplist.getPropList();
	C4PropList *info_proplist = this->info_proplist.getPropList();
	int32_t num_groups = 0;
	// Published properties
	if (info_proplist)
	{
		C4String *default_selection = info_proplist->GetPropertyStr(P_DefaultEditorProp);
		C4Object *obj = info_proplist->GetObject();
		// Properties from effects (no inheritance supported)
		if (obj)
		{
			for (C4Effect *fx = obj->pEffects; fx; fx = fx->pNext)
			{
				if (!fx->IsActive()) continue; // skip dead effects
				QString name = fx->GetName();
				C4PropList *effect_editorprops = fx->GetPropertyPropList(P_EditorProps);
				if (effect_editorprops && AddPropertyGroup(effect_editorprops, num_groups, name, fx, obj, nullptr, nullptr))
					++num_groups;
			}
		}
		// Properties from object (but not on definition)
		if (obj || !info_proplist->GetDef())
		{
			C4PropList *info_editorprops = info_proplist->GetPropertyPropList(P_EditorProps);
			if (info_editorprops)
			{
				QString name;
				C4PropListStatic *proplist_static = info_proplist->IsStatic();
				if (proplist_static)
					name = QString(proplist_static->GetDataString().getData());
				else
					name = info_proplist->GetName();
				if (AddPropertyGroup(info_editorprops, num_groups, name, target_proplist, nullptr, default_selection, default_selection_index))
					++num_groups;
				// Assign group for default selection
				if (*default_selection_index >= 0)
				{
					*default_selection_group = num_groups - 1;
					default_selection = nullptr; // don't find any other instances
				}
			}
		}
		// properties from global list for objects
		if (obj)
		{
			C4Def *editor_base = C4Id2Def(C4ID::EditorBase);
			if (editor_base)
			{
				C4PropList *info_editorprops = editor_base->GetPropertyPropList(P_EditorProps);
				if (AddPropertyGroup(info_editorprops, num_groups, LoadResStr("IDS_CNS_OBJECT"), target_proplist, nullptr, nullptr, nullptr))
					++num_groups;
			}
		}
	}
	// Always: Internal properties
	auto new_properties = target_proplist->GetSortedLocalProperties();
	if (property_groups.size() == num_groups) property_groups.resize(num_groups + 1);
	PropertyGroup &internal_properties = property_groups[num_groups];
	internal_properties.name = LoadResStr("IDS_CNS_INTERNAL");
	internal_properties.props.resize(new_properties.size());
	for (int32_t i = 0; i < new_properties.size(); ++i)
	{
		internal_properties.props[i].parent_value = this->target_value;
		internal_properties.props[i].property_path = target_path;
		internal_properties.props[i].key = new_properties[i];
		internal_properties.props[i].display_name = new_properties[i];
		internal_properties.props[i].delegate_info.Set0(); // default C4Value delegate
		internal_properties.props[i].delegate = NULL; // init when needed
		internal_properties.props[i].group_idx = num_groups;
		internal_properties.props[i].shape.Clear();
		internal_properties.props[i].shape_delegate = nullptr;
	}
	++num_groups;
	return num_groups;
}

int32_t C4ConsoleQtPropListModel::UpdateValueArray(C4ValueArray *target_array, int32_t *default_selection_group, int32_t *default_selection_index)
{
	if (property_groups.empty())
	{
		layout_valid = false;
		property_groups.resize(1);
	}
	C4PropList *info_proplist = this->info_proplist.getPropList();
	C4Value elements_delegate_value;
	if (info_proplist) info_proplist->GetProperty(P_Elements, &elements_delegate_value);
	property_groups[0].name = (info_proplist ? info_proplist->GetName() : LoadResStr("IDS_CNS_ARRAYEDIT"));
	PropertyGroup &properties = property_groups[0];
	if (properties.props.size() != target_array->GetSize())
	{
		layout_valid = false;
		properties.props.resize(target_array->GetSize());
	}
	C4PropertyDelegate *item_delegate = delegate_factory->GetDelegateByValue(elements_delegate_value);
	for (int32_t i = 0; i < properties.props.size(); ++i)
	{
		Property &prop = properties.props[i];
		prop.property_path = C4PropertyPath(target_path, i);
		prop.parent_value = target_value;
		prop.display_name = ::Strings.RegString(FormatString("%d", (int)i).getData());
		prop.key = nullptr;
		prop.delegate_info = elements_delegate_value;
		prop.delegate = item_delegate;
		prop.about_to_edit = false;
		prop.group_idx = 0;
		prop.shape.Clear(); // array elements cannot have shape
		prop.shape_delegate = nullptr;
	}
	return 1; // one group for the values
}

C4ConsoleQtPropListModel::Property *C4ConsoleQtPropListModel::GetPropByIndex(const QModelIndex &index) const
{
	if (!index.isValid()) return nullptr;
	// Resolve group and row
	int32_t group_index = index.internalId(), row = index.row();
	// Prop list access: Properties are on 2nd level
	if (!group_index) return nullptr;
	--group_index;
	if (group_index >= property_groups.size()) return nullptr;
	if (row < 0 || row >= property_groups[group_index].props.size()) return nullptr;
	return const_cast<Property *>(&property_groups[group_index].props[row]);
}

int C4ConsoleQtPropListModel::rowCount(const QModelIndex & parent) const
{
	QModelIndex grandparent;
	// Top level: Property groups
	if (!parent.isValid())
	{
		return property_groups.size();
	}
	// Mid level: Descend into property lists
	grandparent = parent.parent();
	if (!grandparent.isValid())
	{
		if (parent.row() >= 0 && parent.row() < property_groups.size())
			return property_groups[parent.row()].props.size();
	}
	return 0; // no 3rd level depth
}

int C4ConsoleQtPropListModel::columnCount(const QModelIndex & parent) const
{
	return 2; // Name + Data (or Index + Data)
}

QVariant C4ConsoleQtPropListModel::headerData(int section, Qt::Orientation orientation, int role) const
{
	// Table headers
	if (role == Qt::DisplayRole && orientation == Qt::Orientation::Horizontal)
	{
		if (section == 0)
			if (target_value.GetType() == C4V_Array)
				return QVariant(LoadResStr("IDS_CNS_INDEXSHORT"));
			else
				return QVariant(LoadResStr("IDS_CTL_NAME"));
		if (section == 1) return QVariant(LoadResStr("IDS_CNS_VALUE"));
	}
	return QVariant();
}

QVariant C4ConsoleQtPropListModel::data(const QModelIndex & index, int role) const
{
	// Headers
	int32_t group_index = index.internalId();
	if (!group_index)
	{
		if (!index.column())
		{
			if (role == Qt::DisplayRole)
			{
				if (index.row() >= 0 && index.row() < property_groups.size())
					return property_groups[index.row()].name;
			}
			else if (role == Qt::FontRole)
			{
				return header_font;
			}
		}
		return QVariant();
	}
	// Query latest data from prop list
	Property *prop = GetPropByIndex(index);
	if (!prop) return QVariant();
	if (!prop->delegate) prop->delegate = delegate_factory->GetDelegateByValue(prop->delegate_info);
	if (role == Qt::DisplayRole)
	{
		switch (index.column())
		{
		case 0: // First col: Property Name
			return QVariant(prop->display_name->GetCStr());
		case 1: // Second col: Property value
		{
			C4Value v;
			prop->delegate->GetPropertyValue(prop->parent_value, prop->key, index.row(), &v);
			return QVariant(prop->delegate->GetDisplayString(v, target_value.getObj()));
		}
		}
	}
	else if (role == Qt::BackgroundColorRole && index.column()==1)
	{
		C4Value v;
		prop->delegate->GetPropertyValue(prop->parent_value, prop->key, index.row(), &v);
		QColor bgclr = prop->delegate->GetDisplayBackgroundColor(v, target_value.getObj());
		if (bgclr.isValid()) return bgclr;
	}
	else if (role == Qt::TextColorRole && index.column() == 1)
	{
		C4Value v;
		prop->delegate->GetPropertyValue(prop->parent_value, prop->key, index.row(), &v);
		QColor txtclr = prop->delegate->GetDisplayTextColor(v, target_value.getObj());
		if (txtclr.isValid()) return txtclr;
	}
	// Nothing to show
	return QVariant();
}

QModelIndex C4ConsoleQtPropListModel::index(int row, int column, const QModelIndex &parent) const
{
	if (column < 0 || column > 1) return QModelIndex();
	// Top level index?
	if (!parent.isValid())
	{
		// Top level has headers only
		if (row < 0 || row >= property_groups.size()) return QModelIndex();
		return createIndex(row, column, (quintptr)0u);
	}
	if (parent.internalId()) return QModelIndex(); // No 3rd level depth
	// Validate range of property
	const PropertyGroup *property_group = NULL;
	if (parent.row() >= 0 && parent.row() < property_groups.size())
	{
		property_group = &property_groups[parent.row()];
		if (row < 0 || row >= property_group->props.size()) return QModelIndex();
		return createIndex(row, column, (quintptr)parent.row()+1);
	}
	return QModelIndex();
}

QModelIndex C4ConsoleQtPropListModel::parent(const QModelIndex &index) const
{
	// Parent: Stored in internal ID
	auto parent_idx = index.internalId();
	if (parent_idx) return createIndex(parent_idx - 1, 0, (quintptr)0u);
	return QModelIndex();
}

Qt::ItemFlags C4ConsoleQtPropListModel::flags(const QModelIndex &index) const
{
	Qt::ItemFlags flags = QAbstractItemModel::flags(index) | Qt::ItemIsDropEnabled;
	Property *prop = GetPropByIndex(index);
	if (index.isValid() && prop)
	{
		flags &= ~Qt::ItemIsDropEnabled; // only drop between the lines
		if (index.column() == 0)
		{
			// array elements can be re-arranged
			if (prop->parent_value.GetType() == C4V_Array) flags |= Qt::ItemIsDragEnabled;
		}
		else if (index.column() == 1)
		{
			bool readonly = IsTargetReadonly();
			// Only object properties are editable at the moment
			if (!readonly)
				flags |= Qt::ItemIsEditable;
			else
				flags &= ~Qt::ItemIsEnabled;
		}
	}
	return flags;
}

Qt::DropActions C4ConsoleQtPropListModel::supportedDropActions() const
{
	return Qt::MoveAction;
}

bool C4ConsoleQtPropListModel::dropMimeData(const QMimeData *data, Qt::DropAction action, int row, int column, const QModelIndex &parent)
{
	// Drag+Drop movement on array only
	if (action != Qt::MoveAction) return false;
	C4ValueArray *arr = target_value.getArray();
	if (!arr) return false;
	if (!data->hasFormat("application/vnd.text")) return false;
	if (row < 0) return false; // outside range: Could be above or below. Better don't drag at all.
	if (!parent.isValid()) return false; // in array only
	// Decode indices of rows to move
	QByteArray encodedData = data->data("application/vnd.text");
	StdStrBuf rearrange_call;
	rearrange_call.Format("MoveArrayItems(%%s, [%s], %d)", encodedData.constData(), row);
	target_path.DoCall(rearrange_call.getData());
	return true;
}

QStringList C4ConsoleQtPropListModel::mimeTypes() const
{
	QStringList types;
	types << "application/vnd.text";
	return types;
}

QMimeData *C4ConsoleQtPropListModel::mimeData(const QModelIndexList &indexes) const
{
	// Add all moved indexes
	QMimeData *mimeData = new QMimeData();
	QByteArray encodedData;
	int32_t count = 0;
	for (const QModelIndex &index : indexes)
	{
		if (index.isValid() && index.internalId())
		{
			if (count) encodedData.append(",");
			encodedData.append(QString::number(index.row()));
			++count;
		}
	}
	mimeData->setData("application/vnd.text", encodedData);
	return mimeData;
}

const char *C4ConsoleQtPropListModel::GetTargetPathHelp() const
{
	// Help text in EditorInfo prop. Fall back to description.
	C4PropList *info_proplist = this->info_proplist.getPropList();
	if (!info_proplist) return nullptr;
	C4String *desc = info_proplist->GetPropertyStr(P_EditorInfo);
	if (!desc) desc = info_proplist->GetPropertyStr(P_Description);
	if (!desc) return nullptr;
	return desc->GetCStr();
}

void C4ConsoleQtPropListModel::AddArrayElement()
{
	C4Value new_val;
	C4PropList *info_proplist = this->info_proplist.getPropList();
	if (info_proplist) info_proplist->GetProperty(P_DefaultValue, &new_val);
	target_path.DoCall(FormatString("PushBack(%%s, %s)", new_val.GetDataString(10).getData()).getData());
}

void C4ConsoleQtPropListModel::RemoveArrayElement()
{
	// Compose script command to remove all selected array indices
	StdStrBuf script;
	for (QModelIndex idx : selection_model->selectedIndexes())
		if (idx.isValid() && idx.column() == 0)
			if (script.getLength())
				script.AppendFormat(",%d", idx.row());
			else
				script.AppendFormat("%d", idx.row());
	if (script.getLength()) target_path.DoCall(FormatString("RemoveArrayIndices(%%s, [%s])", script.getData()).getData());
}

bool C4ConsoleQtPropListModel::IsTargetReadonly() const
{
	if (target_path.IsEmpty()) return true;
	switch (target_value.GetType())
	{
	case C4V_Array:
		// Arrays are never frozen
		return false;
	case C4V_PropList:
	{
		C4PropList *parent_proplist = target_value._getPropList();
		if (parent_proplist->IsFrozen()) return true;
		return false;
	}
	default:
		return true;
	}
}