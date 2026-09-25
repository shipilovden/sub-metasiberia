/*=====================================================================
GuiClientApplication.cpp
---------------------
Copyright Glare Technologies Limited 2013 -
Generated at Tue May 05 17:30:00 +0200 2013
=====================================================================*/
#include "GuiClientApplication.h"


#include "../utils/ConPrint.h"
#include <QtGui/QFileOpenEvent>
#include <QtWidgets/QMessageBox>
#include <QtCore/QElapsedTimer>



GuiClientApplication::GuiClientApplication(int& argc, char** argv)
:	QApplication(argc, argv)
{

}


QString GuiClientApplication::getOpenFilename()
{
	return filename;
}


bool GuiClientApplication::notify(QObject* receiver, QEvent* event)
{
	static const bool trace_events = qEnvironmentVariableIsSet("METASIBERIA_TRACE_UI_EVENTS");
	if(!trace_events)
		return QApplication::notify(receiver, event);
	// Capture only the class and event number, never widget text or user data.
	const std::string receiver_class = receiver->metaObject()->className();
	const int event_type = (int)event->type();
	QElapsedTimer timer;
	timer.start();
	const bool result = QApplication::notify(receiver, event);
	if(timer.elapsed() >= 100)
		conPrint("Lifecycle event: " + receiver_class + " type=" + std::to_string(event_type) + " ms=" + std::to_string(timer.elapsed()));
	return result;
}


void GuiClientApplication::setOpenFilename(QString filename_)
{
	this->filename = filename_;
}


bool GuiClientApplication::event(QEvent* event)
{
	if (event->type() == QEvent::FileOpen)
	{
		// We need to remember the filename, in case the event comes in before our GUI is fully constructed and the signal is connected.
		setOpenFilename(static_cast<QFileOpenEvent*>(event)->file());
		
		emit openFileOSX(getOpenFilename());
	}
	return QApplication::event(event);
}
