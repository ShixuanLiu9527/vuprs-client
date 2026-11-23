#ifndef VUPRS_CLIENT_FRAME_QUEUE_H
#define VUPRS_CLIENT_FRAME_QUEUE_H

#include <QMutex>
#include <QMutexLocker>
#include <QQueue>
#include <QVector>

namespace vuprs
{
    namespace client
    {
        /**
         * @brief Small bounded thread-safe queue between the network thread and UI.
         */
        template <typename T>
        class FrameQueue
        {
        public:
            explicit FrameQueue(int capacity = 256)
                : capacity_(capacity <= 0 ? 1 : capacity)
            {
            }

            void push(const T &value)
            {
                QMutexLocker locker(&mutex_);
                while (queue_.size() >= capacity_)
                {
                    queue_.dequeue();
                    ++dropped_;
                }
                queue_.enqueue(value);
            }

            bool tryPop(T *value)
            {
                if (value == nullptr)
                    return false;
                QMutexLocker locker(&mutex_);
                if (queue_.isEmpty())
                    return false;
                *value = queue_.dequeue();
                ++taken_;
                return true;
            }

            int drain(QVector<T> *out, int maxItems)
            {
                if (out == nullptr || maxItems <= 0)
                    return 0;
                QMutexLocker locker(&mutex_);
                int count = 0;
                while (!queue_.isEmpty() && count < maxItems)
                {
                    out->push_back(queue_.dequeue());
                    ++count;
                    ++taken_;
                }
                return count;
            }

            int size() const
            {
                QMutexLocker locker(&mutex_);
                return queue_.size();
            }

            quint64 dropped() const
            {
                QMutexLocker locker(&mutex_);
                return dropped_;
            }

            quint64 taken() const
            {
                QMutexLocker locker(&mutex_);
                return taken_;
            }

            void clear()
            {
                QMutexLocker locker(&mutex_);
                queue_.clear();
            }

            int capacity() const { return capacity_; }

        private:
            mutable QMutex mutex_;
            QQueue<T> queue_;
            int capacity_;
            quint64 dropped_ = 0;
            quint64 taken_ = 0;
        };
    }
}

#endif
