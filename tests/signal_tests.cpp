#include "sttnet.h"

#include "test_assert.h"
#include <cstdlib>

int main()
{
    using stt::system::ServerSetting;

    const bool terminationSignalsBlocked=ServerSetting::blockTerminationSignals();
    STTNET_CHECK(terminationSignalsBlocked);
    ServerSetting::setExceptionHandling();

    struct sigaction pipeAction{};
    struct sigaction termAction{};
    const int pipeActionResult=sigaction(SIGPIPE,nullptr,&pipeAction);
    const int termActionResult=sigaction(SIGTERM,nullptr,&termAction);
    STTNET_CHECK(pipeActionResult==0);
    STTNET_CHECK(termActionResult==0);
    STTNET_CHECK(pipeAction.sa_handler==SIG_IGN);
    STTNET_CHECK(termAction.sa_handler!=SIG_IGN);

    const pid_t notifier=fork();
    STTNET_CHECK(notifier>=0);
    if(notifier==0)
    {
        const int killResult=kill(getppid(),SIGTERM);
        _exit(killResult==0?EXIT_SUCCESS:EXIT_FAILURE);
    }

    const int terminationSignal=ServerSetting::waitForTerminationSignal();
    STTNET_CHECK(terminationSignal==SIGTERM);
    int status=0;
    const pid_t notifierWaitResult=waitpid(notifier,&status,0);
    STTNET_CHECK(notifierWaitResult==notifier);
    STTNET_CHECK(WIFEXITED(status));
    STTNET_CHECK(WEXITSTATUS(status)==EXIT_SUCCESS);

    const pid_t interrupter=fork();
    STTNET_CHECK(interrupter>=0);
    if(interrupter==0)
    {
        const int killResult=kill(getppid(),SIGINT);
        _exit(killResult==0?EXIT_SUCCESS:EXIT_FAILURE);
    }
    const int interruptSignal=ServerSetting::waitForTerminationSignal();
    STTNET_CHECK(interruptSignal==SIGINT);
    const pid_t interrupterWaitResult=waitpid(interrupter,&status,0);
    STTNET_CHECK(interrupterWaitResult==interrupter);
    STTNET_CHECK(WIFEXITED(status));
    STTNET_CHECK(WEXITSTATUS(status)==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
