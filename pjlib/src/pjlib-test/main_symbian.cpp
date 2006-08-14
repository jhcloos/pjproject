#include "test.h"
#include <pj/os.h>

#include <e32base.h>
#include <e32std.h>
#include <e32cons.h>            // Console


//  Constants

_LIT(KTextConsoleTitle, "Console");
_LIT(KTextFailed, " failed, leave code = %d");
_LIT(KTextPressAnyKey, " [press any key]\n");


//  Global Variables

LOCAL_D CConsoleBase* console;  // write all messages to this


//  Local Functions

LOCAL_C void MainL()
    {
    //
    // add your program code here, example code below
    //
    console->Write(_L("Hello, world!\n"));
    }


LOCAL_C void DoStartL()
    {
    // Create active scheduler (to run active objects)
    CActiveScheduler* scheduler = new (ELeave) CActiveScheduler();
    CleanupStack::PushL(scheduler);
    CActiveScheduler::Install(scheduler);

    MainL();

    // Delete active scheduler
    CleanupStack::PopAndDestroy(scheduler);
    }


//  Global Functions

GLDEF_C TInt E32Main()
    {
    // Create cleanup stack
    __UHEAP_MARK;
    CTrapCleanup* cleanup = CTrapCleanup::New();

    // Create output console
    TRAPD(createError, console = Console::NewL(KTextConsoleTitle, TSize(KConsFullScreen,KConsFullScreen)));
    if (createError)
        return createError;

    // Run application code inside TRAP harness, wait keypress when terminated
    TRAPD(mainError, DoStartL());
    if (mainError)
        console->Printf(KTextFailed, mainError);
    console->Printf(KTextPressAnyKey);
    console->Getch();
    console->Printf(KTextPressAnyKey);
    console->Getch();
    
    delete console;
    delete cleanup;
    __UHEAP_MARKEND;
    return KErrNone;
    }