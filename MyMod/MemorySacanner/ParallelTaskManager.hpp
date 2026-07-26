#pragma once

#include <thread>
#include <functional>
#include <deque>
#include <atomic>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace  my_mod
{

    extern thread_local std::stop_token stop_token;


    struct IRunningTask
    {
        virtual void callFinisher()=0;
        virtual void callDetachAction()=0;
        virtual ~IRunningTask()=default;

        std::jthread thread;
        std::atomic<bool> finished{false};
    };

    template<typename T>
    struct RunningTask:IRunningTask
    {
        std::function<void(T)> finisher;
        std::function<void(T)> detachAction;
        T data;

        void callFinisher() override
        {
            finisher(std::move(data));
        }

        void callDetachAction() override
        {
            detachAction(std::move(data));
        }

        RunningTask()
        {
            std::cout<<"RunningTask created\n";
        }

        //delete copy and move constructors and assignment operators
        RunningTask(const RunningTask&) = delete;
        RunningTask& operator=(const RunningTask&) = delete;
        RunningTask(RunningTask&&) = delete;
        RunningTask& operator=(RunningTask&&) = delete;



        ~RunningTask() override
        {
            if (thread.joinable())
                thread.join();
            std::cout<<"RunningTask destroyed\n";
        }
    };

    struct IQueuedTask
    {
        virtual std::unique_ptr<IRunningTask> run(std::atomic<int>& runningThreads)=0;
        virtual ~IQueuedTask()=default;
    };

    template<typename T>
    struct QueuedTask:IQueuedTask
    {
        std::function<T(std::stop_token stop_token)> task;
        std::function<void(T)> taskFinisher;
        std::function<void(T)> detachAction;

        std::unique_ptr<IRunningTask> run(std::atomic<int>& runningThreads) override
        {
            ++runningThreads;
            auto running_task=std::make_unique<RunningTask<T>>();
            running_task->finisher=std::move(taskFinisher);
            running_task->detachAction=std::move(detachAction);
            running_task->thread = std::jthread(
                [&runningThreads,task_ptr=running_task.get(),fn=std::move(task)](const std::stop_token& _stop_token) mutable
            {
                my_mod::stop_token=_stop_token;
                task_ptr->data = fn(_stop_token);
                task_ptr->finished=true;
            });


            return running_task;
        };
    };



    class ParallelTaskManager
    {
    protected:
        int maxThreads=1;
        int acceptThreads=1;
        std::atomic<int> runningThreads=0;
        std::deque<std::unique_ptr<IRunningTask>> running_tasks;
        std::deque<std::unique_ptr<IQueuedTask>> queued_tasks;
        std::vector<std::unique_ptr<IRunningTask>> detached_tasks;
        int queueSize=1;

    public:

        explicit ParallelTaskManager(int maxThreads=1, int acceptThreads=1, int queue_size=1):
        maxThreads(maxThreads),acceptThreads(acceptThreads),queueSize(queue_size)
        {
            if(acceptThreads>maxThreads)
                throw std::runtime_error("acceptThreads>maxThreads");
        }

        explicit ParallelTaskManager(int maxThreads=1, int queue_size=1, bool only_finish_last=false):
        maxThreads(maxThreads),queueSize(queue_size)
        {
            if (only_finish_last)
                acceptThreads=1;
            else
                acceptThreads=maxThreads;
        }

        //get result type of task function, for all cases: with stop_token or without it
        template <typename F>
        using task_result_t =
            typename decltype([]{
                if constexpr (std::is_invocable_v<F, std::stop_token>)
                    return std::type_identity<std::invoke_result_t<F, std::stop_token>>{};
                else
                    return std::type_identity<std::invoke_result_t<F>>{};
            }())::type;

        template<typename TaskFunc, typename FinisherFunc>
        void runTask(TaskFunc&& task, FinisherFunc&& taskFinisher)
        {
            using T = task_result_t<TaskFunc>;
            runTask(std::forward<TaskFunc>(task), std::forward<FinisherFunc>(taskFinisher), [](T){});
        }

        template<typename TaskFunc, typename FinisherFunc,typename DetachAction>
        void runTask(TaskFunc&& task, FinisherFunc&& taskFinisher, DetachAction&& detachAction)
        {
            using T = task_result_t<TaskFunc>;

            auto new_task = std::make_unique<QueuedTask<T>>();

            if constexpr (std::is_assignable_v<std::function<T(std::stop_token)>,TaskFunc&&>)
            {
                new_task->task = std::forward<TaskFunc>(task);
            }
            else
            {
                std::function<T(std::stop_token)> tw = [t=std::forward<TaskFunc>(task)](std::stop_token){return t();};
                new_task->task = std::move(tw);
            }

            new_task->taskFinisher = std::forward<FinisherFunc>(taskFinisher);
            new_task->detachAction = std::forward<DetachAction>(detachAction);

            if(runningThreads < maxThreads) // free thread available
            {
                if(static_cast<int>(running_tasks.size()) == acceptThreads)
                {
                    running_tasks.back()->thread.request_stop();
                    detached_tasks.push_back(std::move(running_tasks.back()));
                    running_tasks.pop_back();
                }

                running_tasks.emplace_front(new_task->run(runningThreads));
            }
            else if (queueSize > 0)
            {
                if (static_cast<int>(queued_tasks.size()) == queueSize)
                {
                    queued_tasks.pop_front();
                }
                queued_tasks.emplace_back(std::move(new_task));
            }
        }

        void finish();
        void terminateAll();
        int get_threads_running();

        virtual ~ParallelTaskManager()
        {
            terminateAll();
        }

        bool idle() const
        {
            return running_tasks.empty()&&detached_tasks.empty();
        }

        bool isFree() const
        {
            return running_tasks.empty();
        }
    };
}