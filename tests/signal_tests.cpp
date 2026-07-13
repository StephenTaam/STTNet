#include "sttnet.h"

#include <cassert>
#include <cstdlib>

int main()
{
    using stt::system::ServerSetting;

    assert(ServerSetting::blockTerminationSignals());
    ServerSetting::setExceptionHandling();

    struct sigaction pipeAction{};
    struct sigaction termAction{};
    assert(sigaction(SIGPIPE,nullptr,&pipeAction)==0);
    assert(sigaction(SIGTERM,nullptr,&termAction)==0);
    assert(pipeAction.sa_handler==SIG_IGN);
    assert(termAction.sa_handler!=SIG_IGN);

    const pid_t notifier=fork();
    assert(notifier>=0);
    if(notifier==0)
    {
        kill(getppid(),SIGTERM);
        _exit(EXIT_SUCCESS);
    }

    assert(ServerSetting::waitForTerminationSignal()==SIGTERM);
    int status=0;
    assert(waitpid(notifier,&status,0)==notifier);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status)==EXIT_SUCCESS);

    const pid_t interrupter=fork();
    assert(interrupter>=0);
    if(interrupter==0)
    {
        kill(getppid(),SIGINT);
        _exit(EXIT_SUCCESS);
    }
    assert(ServerSetting::waitForTerminationSignal()==SIGINT);
    assert(waitpid(interrupter,&status,0)==interrupter);
    assert(WIFEXITED(status));
    assert(WEXITSTATUS(status)==EXIT_SUCCESS);
    return EXIT_SUCCESS;
}
