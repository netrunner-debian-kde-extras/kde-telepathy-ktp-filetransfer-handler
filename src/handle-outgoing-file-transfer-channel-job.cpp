/*
* Copyright (C) 2010, 2011 Daniele E. Domenichelli <daniele.domenichelli@gmail.com>
*
* This library is free software; you can redistribute it and/or
* modify it under the terms of the GNU Lesser General Public
* License as published by the Free Software Foundation; either
* version 2.1 of the License, or (at your option) any later version.
*
* This library is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* Lesser General Public License for more details.
*
* You should have received a copy of the GNU Lesser General Public
* License along with this library; if not, write to the Free Software
* Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
*/


#include "handle-outgoing-file-transfer-channel-job.h"
#include "telepathy-base-job_p.h"
#include "ktp-fth-debug.h"

#include <QTimer>
#include <QDebug>
#include <QUrl>

#include <KLocalizedString>
#include <kio/global.h>
#include <kjobtrackerinterface.h>

#include <TelepathyQt/OutgoingFileTransferChannel>
#include <TelepathyQt/PendingReady>
#include <TelepathyQt/PendingOperation>
#include <TelepathyQt/Contact>

class HandleOutgoingFileTransferChannelJobPrivate : public KTp::TelepathyBaseJobPrivate
{
    Q_DECLARE_PUBLIC(HandleOutgoingFileTransferChannelJob)

public:
    HandleOutgoingFileTransferChannelJobPrivate();
    virtual ~HandleOutgoingFileTransferChannelJobPrivate();

    Tp::OutgoingFileTransferChannelPtr channel;
    QFile* file;
    QUrl uri;
    qulonglong offset;

    void init();
    bool kill();
    void provideFile();

    void __k__start();
    void __k__onInitialOffsetDefined(qulonglong offset);
    void __k__onFileTransferChannelStateChanged(Tp::FileTransferState state, Tp::FileTransferStateChangeReason reason);
    void __k__onFileTransferChannelTransferredBytesChanged(qulonglong count);
    void __k__onProvideFileFinished(Tp::PendingOperation* op);
    void __k__onCancelOperationFinished(Tp::PendingOperation* op);
    void __k__onInvalidated();
};

HandleOutgoingFileTransferChannelJob::HandleOutgoingFileTransferChannelJob(Tp::OutgoingFileTransferChannelPtr channel,
                                                                           QObject* parent)
    : TelepathyBaseJob(*new HandleOutgoingFileTransferChannelJobPrivate(), parent)
{
    qCDebug(KTP_FTH_MODULE);
    Q_D(HandleOutgoingFileTransferChannelJob);

    d->channel = channel;
    d->init();
}

HandleOutgoingFileTransferChannelJob::~HandleOutgoingFileTransferChannelJob()
{
    KIO::getJobTracker()->unregisterJob(this);
    qCDebug(KTP_FTH_MODULE);
}

void HandleOutgoingFileTransferChannelJob::start()
{
    qCDebug(KTP_FTH_MODULE);
    KIO::getJobTracker()->registerJob(this);
    // KWidgetJobTracker has an internal timer of 500 ms, if we don't wait here
    // when the job description is emitted it won't be ready
    QTimer::singleShot(500, this, SLOT(__k__start()));
}

bool HandleOutgoingFileTransferChannelJob::doKill()
{
    qCDebug(KTP_FTH_MODULE) << "Outgoing file transfer killed.";
    Q_D(HandleOutgoingFileTransferChannelJob);
    return d->kill();
}

HandleOutgoingFileTransferChannelJobPrivate::HandleOutgoingFileTransferChannelJobPrivate()
    : file(0),
      offset(0)
{
    qCDebug(KTP_FTH_MODULE);
}

HandleOutgoingFileTransferChannelJobPrivate::~HandleOutgoingFileTransferChannelJobPrivate()
{
    qCDebug(KTP_FTH_MODULE);
}

void HandleOutgoingFileTransferChannelJobPrivate::init()
{
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    if (channel.isNull()) {
        qCritical() << "Channel cannot be NULL";
        q->setError(KTp::NullChannel);
        q->setErrorText(i18n("Invalid channel"));
        QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
        return;
    }

    Tp::Features features = Tp::Features() << Tp::FileTransferChannel::FeatureCore;
    if (!channel->isReady(Tp::Features() << Tp::FileTransferChannel::FeatureCore)) {
        qCritical() << "Channel must be ready with Tp::FileTransferChannel::FeatureCore";
        q->setError(KTp::FeatureNotReady);
        q->setErrorText(i18n("Channel is not ready"));
        QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
        return;
    }

    uri = QUrl(channel->uri());
    if (uri.isEmpty()) {
        qCWarning(KTP_FTH_MODULE) << "URI property missing";
        q->setError(KTp::UriPropertyMissing);
        q->setErrorText(i18n("URI property is missing"));
        QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
        return;
    }
    if (!uri.isLocalFile()) {
        // TODO handle this!
        qCWarning(KTP_FTH_MODULE) << "Not a local file";
        q->setError(KTp::NotALocalFile);
        q->setErrorText(i18n("This is not a local file"));
        QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
        return;
    }

    q->setCapabilities(KJob::Killable);
    q->setTotalAmount(KJob::Bytes, channel->size());
    q->setProcessedAmountAndCalculateSpeed(0);

    q->connect(channel.data(),
               SIGNAL(invalidated(Tp::DBusProxy*,QString,QString)),
               SLOT(__k__onInvalidated()));
    q->connect(channel.data(),
               SIGNAL(initialOffsetDefined(qulonglong)),
               SLOT(__k__onInitialOffsetDefined(qulonglong)));
    q->connect(channel.data(),
               SIGNAL(stateChanged(Tp::FileTransferState,Tp::FileTransferStateChangeReason)),
               SLOT(__k__onFileTransferChannelStateChanged(Tp::FileTransferState,Tp::FileTransferStateChangeReason)));
    q->connect(channel.data(),
               SIGNAL(transferredBytesChanged(qulonglong)),
               SLOT(__k__onFileTransferChannelTransferredBytesChanged(qulonglong)));
}

void HandleOutgoingFileTransferChannelJobPrivate::__k__start()
{
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    Q_ASSERT(!q->error());
    if (q->error()) {
        qCWarning(KTP_FTH_MODULE) << "Job was started in error state. Something wrong happened." << q->errorString();
        QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
        return;
    }

    Q_EMIT q->description(q, i18n("Outgoing file transfer"),
                          qMakePair<QString, QString>(i18n("To"), channel->targetContact()->alias()),
                          qMakePair<QString, QString>(i18n("Filename"), channel->uri()));

    if (channel->state() == Tp::FileTransferStateAccepted) {
        provideFile();
    }
}

bool HandleOutgoingFileTransferChannelJobPrivate::kill()
{
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    if (channel->state() != Tp::FileTransferStateCancelled) {
        Tp::PendingOperation *cancelOperation = channel->cancel();
        q->connect(cancelOperation,
                   SIGNAL(finished(Tp::PendingOperation*)),
                   SLOT(__k__onCancelOperationFinished(Tp::PendingOperation*)));
    } else {
        QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
    }

    return true;
}

void HandleOutgoingFileTransferChannelJobPrivate::__k__onInitialOffsetDefined(qulonglong offset)
{
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    this->offset = offset;
    q->setProcessedAmountAndCalculateSpeed(offset);
}

void HandleOutgoingFileTransferChannelJobPrivate::__k__onFileTransferChannelStateChanged(Tp::FileTransferState state,
                                                                                         Tp::FileTransferStateChangeReason stateReason)
{
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    qCDebug(KTP_FTH_MODULE) << "Outgoing file transfer channel state changed to" << state << "with reason" << stateReason;

    switch (state) {
    case Tp::FileTransferStateNone:
        // This is bad
        qCWarning(KTP_FTH_MODULE) << "An unknown error occurred.";
        q->setError(KTp::TelepathyErrorError);
        q->setErrorText(i18n("An unknown error occurred"));
        QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
        break;
    case Tp::FileTransferStateCompleted:
        qCDebug(KTP_FTH_MODULE) << "Outgoing file transfer completed";
        Q_EMIT q->infoMessage(q, i18n("Outgoing file transfer")); // [Finished] is added automatically to the notification
        QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
        break;
    case Tp::FileTransferStateCancelled:
        q->setError(KTp::FileTransferCancelled);
        q->setErrorText(i18n("Outgoing file transfer was canceled."));
        q->kill(KJob::Quietly);
        break;
    case Tp::FileTransferStateAccepted:
        provideFile();
        break;
    case Tp::FileTransferStatePending:
    case Tp::FileTransferStateOpen:
    default:
        break;
    }
}

void HandleOutgoingFileTransferChannelJobPrivate::provideFile()
{
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    file = new QFile(uri.toLocalFile(), q->parent());
    qCDebug(KTP_FTH_MODULE) << "Providing file" << file->fileName();

    Tp::PendingOperation* provideFileOperation = channel->provideFile(file);
    q->connect(provideFileOperation,
               SIGNAL(finished(Tp::PendingOperation*)),
               SLOT(__k__onProvideFileFinished(Tp::PendingOperation*)));
}

void HandleOutgoingFileTransferChannelJobPrivate::__k__onFileTransferChannelTransferredBytesChanged(qulonglong count)
{
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    qCDebug(KTP_FTH_MODULE).nospace() << "Sending " << channel->fileName() << " - "
                       << "Transferred bytes = " << offset + count << " ("
                       << ((int)(((double)(offset + count) / channel->size()) * 100)) << "% done)";
    q->setProcessedAmountAndCalculateSpeed(offset + count);
}

void HandleOutgoingFileTransferChannelJobPrivate::__k__onProvideFileFinished(Tp::PendingOperation* op)
{
    // This method is called when the "provideFile" operation is finished,
    // therefore the file was not sent yet.
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    if (op->isError()) {
        qCWarning(KTP_FTH_MODULE) << "Unable to provide file - " << op->errorName() << ":" << op->errorMessage();
        q->setError(KTp::ProvideFileError);
        q->setErrorText(i18n("Cannot provide file"));
        QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
    }
}

void HandleOutgoingFileTransferChannelJobPrivate::__k__onCancelOperationFinished(Tp::PendingOperation* op)
{
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    if (op->isError()) {
        qCWarning(KTP_FTH_MODULE) << "Unable to cancel file transfer - " << op->errorName() << ":" << op->errorMessage();
        q->setError(KTp::CancelFileTransferError);
        q->setErrorText(i18n("Cannot cancel outgoing file transfer"));
    }

    qCDebug(KTP_FTH_MODULE) << "File transfer cancelled";
    QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
}

void HandleOutgoingFileTransferChannelJobPrivate::__k__onInvalidated()
{
    qCDebug(KTP_FTH_MODULE);
    Q_Q(HandleOutgoingFileTransferChannelJob);

    qCWarning(KTP_FTH_MODULE) << "File transfer invalidated!" << channel->invalidationMessage() << "reason" << channel->invalidationReason();
    Q_EMIT q->infoMessage(q, i18n("File transfer invalidated. %1", channel->invalidationMessage()));

    QTimer::singleShot(0, q, SLOT(__k__doEmitResult()));
}

#include "moc_handle-outgoing-file-transfer-channel-job.cpp"
