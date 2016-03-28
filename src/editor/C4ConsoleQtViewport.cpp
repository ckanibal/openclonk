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

/* Player and editor viewports in console */

#include <C4Include.h>
#include <C4Value.h>
#include <C4ConsoleQtViewport.h>
#include <C4ConsoleQtState.h>
#include <C4Viewport.h>
#include <C4ViewportWindow.h>
#include <C4Console.h>
#include <C4MouseControl.h>
#include <C4Landscape.h>

/* Console viewports */

C4ConsoleQtViewportView::C4ConsoleQtViewportView(class C4ConsoleQtViewportScrollArea *scrollarea)
	: QOpenGLWidget(scrollarea->dock), dock(scrollarea->dock), cvp(scrollarea->cvp)
{
	setAttribute(Qt::WA_ShowWithoutActivating, true);
	setFocusPolicy(Qt::WheelFocus);
	setMouseTracking(true);
	// Register for viewport
	C4ViewportWindow *window = dock->cvp;
	window->glwidget = this;
}

bool C4ConsoleQtViewportView::IsPlayViewport() const
{
	return (cvp && ::MouseControl.IsViewport(cvp)
		&& (::Console.EditCursor.GetMode() == C4CNS_ModePlay));
}

bool C4ConsoleQtViewportView::nativeEvent(const QByteArray &eventType, void *message, long *result)
{
	// Handle native Windows messages
#ifdef USE_WIN32_WINDOWS
	MSG *msg = static_cast<MSG*>(message);
	switch (msg->message)
	{
	//----------------------------------------------------------------------------------------------------------------------------------
	case WM_HSCROLL:
		switch (LOWORD(msg->wParam))
		{
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: cvp->SetViewX(float(HIWORD(msg->wParam)) / cvp->GetZoom()); break;
		case SB_LINELEFT: cvp->ScrollView(-ViewportScrollSpeed, 0.0f); break;
		case SB_LINERIGHT: cvp->ScrollView(+ViewportScrollSpeed, 0.0f); break;
		case SB_PAGELEFT: cvp->ScrollView(-cvp->ViewWdt / cvp->GetZoom(), 0.0f); break;
		case SB_PAGERIGHT: cvp->ScrollView(+cvp->ViewWdt / cvp->GetZoom(), 0.0f); break;
		}
		cvp->Execute();
		cvp->ScrollBarsByViewPosition();
		return true;
	//----------------------------------------------------------------------------------------------------------------------------------
	case WM_VSCROLL:
		switch (LOWORD(msg->wParam))
		{
		case SB_THUMBTRACK:
		case SB_THUMBPOSITION: cvp->SetViewY(float(HIWORD(msg->wParam)) / cvp->GetZoom()); break;
		case SB_LINEUP: cvp->ScrollView(0.0f, -ViewportScrollSpeed); break;
		case SB_LINEDOWN: cvp->ScrollView(0.0f, +ViewportScrollSpeed); break;
		case SB_PAGEUP: cvp->ScrollView(0.0f, -cvp->ViewWdt / cvp->GetZoom()); break;
		case SB_PAGEDOWN: cvp->ScrollView(0.0f, +cvp->ViewWdt / cvp->GetZoom()); break;
		}
		cvp->Execute();
		cvp->ScrollBarsByViewPosition();
		return true;
		//----------------------------------------------------------------------------------------------------------------------------------
	}
#endif
	return false;
}

// Get Shift state as Win32 wParam
uint32_t GetShiftWParam()
{
	auto modifiers = QGuiApplication::keyboardModifiers();
	uint32_t result = 0;
	if (modifiers & Qt::ShiftModifier) result |= MK_SHIFT;
	if (modifiers & Qt::ControlModifier) result |= MK_CONTROL;
	if (modifiers & Qt::AltModifier) result |= MK_ALT;
	return result;
}

void C4ConsoleQtViewportView::mouseMoveEvent(QMouseEvent *eventMove)
{
	if (IsPlayViewport())
	{
		bool is_in_drawrange = (Inside<int32_t>(eventMove->x() - cvp->DrawX, 0, cvp->ViewWdt - 1)
		                     && Inside<int32_t>(eventMove->y() - cvp->DrawY, 0, cvp->ViewHgt - 1));
		this->setCursor(is_in_drawrange ? Qt::BlankCursor : Qt::CrossCursor);
		C4GUI::MouseMove(C4MC_Button_None, eventMove->x(), eventMove->y(), GetShiftWParam(), cvp);
	}
	else
	{
		this->setCursor(Qt::CrossCursor);
		cvp->pWindow->EditCursorMove(eventMove->x(), eventMove->y(), GetShiftWParam());
	}

}

void C4ConsoleQtViewportView::mousePressEvent(QMouseEvent *eventPress)
{
	if (IsPlayViewport())
	{
		int32_t btn = C4MC_Button_None;
		switch (eventPress->button())
		{
		case Qt::LeftButton: btn = C4MC_Button_LeftDown; break;
		case Qt::RightButton: btn = C4MC_Button_RightDown; break;
		}
		C4GUI::MouseMove(btn, eventPress->x(), eventPress->y(), GetShiftWParam(), cvp);
	}
	else
	{
		// movement update needed before, so target is always up-to-date
		cvp->pWindow->EditCursorMove(eventPress->x(), eventPress->y(), GetShiftWParam());
		switch (eventPress->button())
		{
		case Qt::LeftButton: ::Console.EditCursor.LeftButtonDown(GetShiftWParam()); break;
		case Qt::RightButton: ::Console.EditCursor.RightButtonDown(GetShiftWParam()); break;
		}
	}
}

void C4ConsoleQtViewportView::mouseDoubleClickEvent(QMouseEvent *eventPress)
{
	if (IsPlayViewport())
	{
		int32_t btn = C4MC_Button_None;
		switch (eventPress->button())
		{
		case Qt::LeftButton: btn = C4MC_Button_LeftDouble; break;
		case Qt::RightButton: btn = C4MC_Button_RightDouble; break;
		}
		C4GUI::MouseMove(btn, eventPress->x(), eventPress->y(), GetShiftWParam(), cvp);
	}
}

void C4ConsoleQtViewportView::mouseReleaseEvent(QMouseEvent *releaseEvent)
{
	if (IsPlayViewport())
	{
		int32_t btn = C4MC_Button_None;
		switch (releaseEvent->button())
		{
		case Qt::LeftButton: btn = C4MC_Button_LeftUp; break;
		case Qt::RightButton: btn = C4MC_Button_RightUp; break;
		}
		C4GUI::MouseMove(btn, releaseEvent->x(), releaseEvent->y(), GetShiftWParam(), cvp);
	}
	else
	{
		switch (releaseEvent->button())
		{
		case Qt::LeftButton: ::Console.EditCursor.LeftButtonUp(GetShiftWParam()); break;
		case Qt::RightButton: ::Console.EditCursor.RightButtonUp(GetShiftWParam()); break;
		}
	}
}

void C4ConsoleQtViewportView::wheelEvent(QWheelEvent *event)
{
	if (IsPlayViewport())
	{
		int delta = event->delta() / 8;
		if (!delta) delta = event->delta(); // abs(delta)<8?
		uint32_t shift = (delta>0) ? (delta<<16) : uint32_t(delta<<16);
		shift += GetShiftWParam();
		C4GUI::MouseMove(C4MC_Button_Wheel, event->x(), event->y(), shift, cvp);
	}
	else
	{
		auto delta = event->angleDelta();
		auto modifiers = QGuiApplication::keyboardModifiers();
		// Zoom with Ctrl + mouse wheel, just like for player viewports.
		if (modifiers & Qt::ControlModifier)
			cvp->ChangeZoom(pow(C4GFX_ZoomStep, (float) delta.y() / 120));
		else
		{
			// Viewport movement.
			float x = -ViewportScrollSpeed * delta.x() / 120, y = -ViewportScrollSpeed * delta.y() / 120;
			// Not everyone has a vertical scroll wheel...
			if (modifiers & Qt::ShiftModifier)
				std::swap(x, y);
			cvp->ScrollView(x, y);
		}
	}
}

void C4ConsoleQtViewportView::focusInEvent(QFocusEvent * event)
{
	dock->OnActiveChanged(true);
	QWidget::focusInEvent(event);
}

void C4ConsoleQtViewportView::focusOutEvent(QFocusEvent * event)
{
	dock->OnActiveChanged(false);
	QWidget::focusOutEvent(event);
}



/* Keyboard scan code mapping from Qt to our keys */

/** Convert certain keys to (unix(?)) scancodes (those that differ from scancodes on Windows. Sometimes. Maybe.) */

static C4KeyCode QtKeyToUnixScancode(const QKeyEvent &event)
{
	//LogF("VK: %x   SC: %x    key: %x", event.nativeVirtualKey(), event.nativeScanCode(), event.key());
	// Map some special keys
	switch (event.key())
	{
	case Qt::Key_Home:		return K_HOME;
	case Qt::Key_End:		return K_END;
	case Qt::Key_PageUp:	return K_PAGEUP;
	case Qt::Key_PageDown:	return K_PAGEDOWN;
	case Qt::Key_Up:		return K_UP;
	case Qt::Key_Down:		return K_DOWN;
	case Qt::Key_Left:		return K_LEFT;
	case Qt::Key_Right:		return K_RIGHT;
	case Qt::Key_Clear:		return K_CENTER;
	case Qt::Key_Insert:	return K_INSERT;
	case Qt::Key_Delete:	return K_DELETE;
	case Qt::Key_Menu:		return K_MENU;
	case Qt::Key_Pause:		return K_PAUSE;
	case Qt::Key_Print:		return K_PRINT;
	case Qt::Key_NumLock:	return K_NUM;
	case Qt::Key_ScrollLock:return K_SCROLL;
	default:
		// Some native Win32 key mappings...
#ifdef USE_WIN32_WINDOWS
		switch (event.nativeVirtualKey())
		{
		case VK_LWIN:		return K_WIN_L;
		case VK_RWIN:		return K_WIN_R;
		case VK_NUMPAD1:	return K_NUM1;
		case VK_NUMPAD2:	return K_NUM2;
		case VK_NUMPAD3:	return K_NUM3;
		case VK_NUMPAD4:	return K_NUM4;
		case VK_NUMPAD5:	return K_NUM5;
		case VK_NUMPAD6:	return K_NUM6;
		case VK_NUMPAD7:	return K_NUM7;
		case VK_NUMPAD8:	return K_NUM8;
		case VK_NUMPAD9:	return K_NUM9;
		case VK_NUMPAD0:	return K_NUM0;
		}
		switch (event.nativeScanCode())
		{
		case 285: return K_CONTROL_R;
		}
#endif
		// Otherwise rely on native scan code to be the same on all platforms
#ifdef Q_OS_LINUX
		return event.nativeScanCode() - 8;
#elif defined(Q_OS_DARWIN)
		TODO: nativeScanCode() apparently doesn't work on OS X.
#else
		return event.nativeScanCode();
#endif
	}
}

void C4ConsoleQtViewportView::keyPressEvent(QKeyEvent * event)
{
	// Convert key to our internal mapping
	C4KeyCode code = QtKeyToUnixScancode(*event);
	// Viewport-only handling
	bool handled = false;
	if (code == K_SCROLL)
	{
		cvp->TogglePlayerLock();
		handled = true;
	}
	// Handled if handled as player control or main editor
	if (!handled) handled = Game.DoKeyboardInput(code, KEYEV_Down, !!(event->modifiers() & Qt::AltModifier), !!(event->modifiers() & Qt::ControlModifier), !!(event->modifiers() & Qt::ShiftModifier), event->isAutoRepeat(), NULL);
	if (!handled) handled = dock->main_window->HandleEditorKeyDown(event);
	event->setAccepted(handled);
}

void C4ConsoleQtViewportView::keyReleaseEvent(QKeyEvent * event)
{
	// Convert key to our internal mapping
	C4KeyCode code = QtKeyToUnixScancode(*event);
	// Handled if handled as player control
	bool handled = Game.DoKeyboardInput(code, KEYEV_Up, !!(event->modifiers() & Qt::AltModifier), !!(event->modifiers() & Qt::ControlModifier), !!(event->modifiers() & Qt::ShiftModifier), event->isAutoRepeat(), NULL);
	if (!handled) handled = dock->main_window->HandleEditorKeyUp(event);
	event->setAccepted(handled);
}

void C4ConsoleQtViewportView::enterEvent(QEvent *)
{
	// TODO: This should better be managed by the viewport
	// looks weird when there's multiple viewports open
	// but for some reason, the EditCursor drawing stuff is not associated with the viewport (yet)
	::Console.EditCursor.SetMouseHover(true);
}

void C4ConsoleQtViewportView::leaveEvent(QEvent *)
{
	// TODO: This should better be managed by the viewport
	::Console.EditCursor.SetMouseHover(false);
}

void C4ConsoleQtViewportView::initializeGL()
{
	// init extensions
	glewExperimental = GL_TRUE;
	GLenum err = glewInit();
	if (GLEW_OK != err)
	{
		// Problem: glewInit failed, something is seriously wrong.
		LogF("glewInit: %s", reinterpret_cast<const char*>(glewGetErrorString(err)));
	}
}

void C4ConsoleQtViewportView::resizeGL(int w, int h)
{
	cvp->UpdateOutputSize(w, h);
}

void C4ConsoleQtViewportView::paintGL()
{
	cvp->ScrollBarsByViewPosition();
	cvp->Execute();
}


C4ConsoleQtViewportScrollArea::C4ConsoleQtViewportScrollArea(class C4ConsoleQtViewportDockWidget *dock)
	: QAbstractScrollArea(dock), dock(dock), cvp(dock->cvp->cvp)
{
	cvp->scrollarea = this;
	// No scroll bars by default. Neutral viewports will toggle this.
	setScrollBarVisibility(false);
}

void C4ConsoleQtViewportScrollArea::scrollContentsBy(int dx, int dy)
{
	// Just use the absolute position in any case.
	cvp->SetViewX(horizontalScrollBar()->value());
	cvp->SetViewY(verticalScrollBar()->value());
}

bool C4ConsoleQtViewportScrollArea::viewportEvent(QEvent *e)
{
	// Pass everything to the viewport.
	return false;
}

void C4ConsoleQtViewportScrollArea::setupViewport(QWidget *viewport)
{
	// Don't steal focus from the viewport. This is necessary to make keyboard input work.
	viewport->setFocusProxy(NULL);
	ScrollBarsByViewPosition();
}

void C4ConsoleQtViewportScrollArea::ScrollBarsByViewPosition()
{
	int x = viewport()->width() / cvp->GetZoom();
	horizontalScrollBar()->setRange(0, GBackWdt - x);
	horizontalScrollBar()->setPageStep(x);
	horizontalScrollBar()->setValue(cvp->GetViewX());

	int y = viewport()->height() / cvp->GetZoom();
	verticalScrollBar()->setRange(0, GBackHgt - y);
	verticalScrollBar()->setPageStep(y);
	verticalScrollBar()->setValue(cvp->GetViewY());
}

void C4ConsoleQtViewportScrollArea::setScrollBarVisibility(bool visible)
{
	if (visible)
	{
		setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOn);
	}
	else
	{
		setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
		setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
	}
}


C4ConsoleQtViewportLabel::C4ConsoleQtViewportLabel(const QString &title, C4ConsoleQtViewportDockWidget *dock)
	: QLabel(dock), dock(dock)
{
	OnActiveChanged(false);
	setText(title);
}

void C4ConsoleQtViewportLabel::OnActiveChanged(bool active)
{
	// set color schemes for inactive / active viewport headers
	QColor bgclr = QApplication::palette(this).color(QPalette::Highlight);
	QColor fontclr = QApplication::palette(this).color(QPalette::HighlightedText);
	if (active)
		setStyleSheet(QString(
			"QLabel { background: %1; padding: 3px; color: %2; font-weight: bold; }")
			.arg(bgclr.name(), fontclr.name()));
	else
		setStyleSheet(QString(
			"QLabel { padding: 3px; }"));
}

void C4ConsoleQtViewportLabel::mousePressEvent(QMouseEvent *eventPress)
{
	dock->view->setFocus();
	QLabel::mousePressEvent(eventPress);
}



C4ConsoleQtViewportDockWidget::C4ConsoleQtViewportDockWidget(C4ConsoleQtMainWindow *main_window, QMainWindow *parent, C4ViewportWindow *cvp)
	: QDockWidget("", parent), main_window(main_window), cvp(cvp)
{
	// Translated title
	setWindowTitle(LoadResStr("IDS_CNS_VIEWPORT"));
	// Actual view container, wrapped in scrolling area
	auto scrollarea = new C4ConsoleQtViewportScrollArea(this);
	view = new C4ConsoleQtViewportView(scrollarea);
	scrollarea->setViewport(view);
	setTitleBarWidget((title_label = new C4ConsoleQtViewportLabel(windowTitle(), this)));
	setWidget(scrollarea);
	connect(this, SIGNAL(topLevelChanged(bool)), this, SLOT(TopLevelChanged(bool)));
	// Register viewport widget for periodic rendering updates.
	cvp->viewport_widget = view;
}

void C4ConsoleQtViewportDockWidget::mousePressEvent(QMouseEvent *eventPress)
{
	// Clicking the dock focuses the viewport
	view->setFocus();
	QDockWidget::mousePressEvent(eventPress);
}

void C4ConsoleQtViewportDockWidget::OnActiveChanged(bool active)
{
	title_label->OnActiveChanged(active);
}

void C4ConsoleQtViewportDockWidget::TopLevelChanged(bool is_floating)
{
	if (!is_floating)
	{
		// docked has custom title
		setTitleBarWidget(title_label);
	}
	else
	{
		// undocked using OS title
		setTitleBarWidget(NULL);
	}
	// Ensure focus after undock and after re-docking floating viewport window
	view->setFocus();
}

void C4ConsoleQtViewportDockWidget::closeEvent(QCloseEvent * event)
{
	QDockWidget::closeEvent(event);
	if (event->isAccepted())
	{
		if (cvp) cvp->Close();
		cvp = NULL;
		deleteLater();
	}
}