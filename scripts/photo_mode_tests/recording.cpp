#include "../../gui_client/PhotoVideoRecorder.h"
#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QThread>
#include <QtGui/QPainter>
#include <cstdio>

int main(int argc,char** argv) {
	QCoreApplication app(argc,argv);
	if(argc!=2) return 2;
	const QString path=QString::fromLocal8Bit(argv[1]);
	if(QFileInfo::exists(path)) return 3; // No overwrites, even in the smoke test.
	QImage frame(320,240,QImage::Format_RGB888);
	frame.fill(Qt::blue);
	{ QPainter p(&frame); p.fillRect(0,0,160,120,Qt::red); p.fillRect(160,0,160,120,Qt::green); }
	PhotoVideoRecorder recorder(path,30,2000000,frame);
	QThread::msleep(500);
	recorder.submit(frame); // Deliberately sparse capture: duration must remain real time.
	QThread::msleep(600);
	recorder.stop();
	QElapsedTimer deadline; deadline.start();
	while(!recorder.finished() && deadline.elapsed()<15000) QThread::msleep(20);
	if(!recorder.finished() || !recorder.error().isEmpty()) {
		std::fprintf(stderr,"FAIL: %s\n",recorder.error().toUtf8().constData()); return 1;
	}
	if(QFileInfo(path).size()<1000) return 4;
	// A failed sink must report its error instead of leaving the UI recording forever.
	PhotoVideoRecorder invalid(path+"/missing/output.mp4",30,2000000,frame);
	deadline.restart();
	while(!invalid.finished() && deadline.elapsed()<15000) QThread::msleep(20);
	if(!invalid.finished() || invalid.error().isEmpty()) return 5;
	std::puts("PASS: worker capture, sparse frame timing, stop and MP4 finalisation");
	return 0;
}
