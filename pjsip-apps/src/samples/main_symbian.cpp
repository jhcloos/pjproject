#include <stdlib.h>
#include <stdio.h>
#include <e32std.h>
#include <e32base.h>
#include <e32std.h>
#include <e32cons.h>            // Console

/* PJLIB Unicode conversion */
#include <pj/unicode.h>

/* Log writing */
#include <pj/log.h>


/* This will be defined in application's code */
extern "C"
{
	int pj_app_main(int argc, char *argv[]);
}



//  Global Variables

LOCAL_D CConsoleBase* console;  // write all messages to this


//  Local Functions

LOCAL_C void MainL()
{
    //
    // add your program code here, example code below
    //
    char *argv[] = { "main_symbian", NULL };
    int rc = pj_app_main(1, argv);

    console->Printf(_L(" [press any key]\n"));
    console->Getch();

    CActiveScheduler::Stop();
}

class MyScheduler : public CActiveScheduler
{
public:
    MyScheduler()
    {}

    void Error(TInt aError) const;
};

void MyScheduler::Error(TInt aError) const
{
    int i = 0;
}

class ProgramStarter : public CActive
{
public:
    static ProgramStarter *NewL();
    void Start();

protected:
    ProgramStarter();
    void ConstructL();
    virtual void RunL();
    virtual void DoCancel();
    TInt RunError(TInt aError);

private:
    RTimer timer_;
};

ProgramStarter::ProgramStarter()
: CActive(EPriorityNormal)
{
}

void ProgramStarter::ConstructL()
{
    timer_.CreateLocal();
    CActiveScheduler::Add(this);
}

ProgramStarter *ProgramStarter::NewL()
{
    ProgramStarter *self = new (ELeave) ProgramStarter;
    CleanupStack::PushL(self);

    self->ConstructL();

    CleanupStack::Pop(self);
    return self;
}

void ProgramStarter::Start()
{
    timer_.After(iStatus, 0);
    SetActive();
}

void ProgramStarter::RunL()
{
    MainL();
}

void ProgramStarter::DoCancel()
{
}

TInt ProgramStarter::RunError(TInt aError)
{
    PJ_UNUSED_ARG(aError);
    return KErrNone;
}


LOCAL_C void DoStartL()
{
    // Create active scheduler (to run active objects)
    CActiveScheduler* scheduler = new (ELeave) MyScheduler;
    CleanupStack::PushL(scheduler);
    CActiveScheduler::Install(scheduler);

    ProgramStarter *starter = ProgramStarter::NewL();
    starter->Start();

    CActiveScheduler::Start();
}


//  Global Functions

static void log_writer(int level, const char *buf, int len)
{
    wchar_t buf16[PJ_LOG_MAX_SIZE];

    pj_ansi_to_unicode(buf, len, buf16, PJ_ARRAY_SIZE(buf16));

    TPtrC16 aBuf((const TUint16*)buf16, (TInt)len);
    console->Write(aBuf);
}


GLDEF_C TInt E32Main()
{
    // Create cleanup stack
    __UHEAP_MARK;
    CTrapCleanup* cleanup = CTrapCleanup::New();

    // Create output console
    TRAPD(createError, console = Console::NewL(_L("Console"), TSize(KConsFullScreen,KConsFullScreen)));
    if (createError)
        return createError;

    pj_log_set_log_func(&log_writer);

    // Run application code inside TRAP harness, wait keypress when terminated
    TRAPD(mainError, DoStartL());
    if (mainError)
        console->Printf(_L(" failed, leave code = %d"), mainError);
    console->Printf(_L(" [press any key]\n"));
    console->Getch();
    
    delete console;
    delete cleanup;
    __UHEAP_MARKEND;
    return KErrNone;
}


