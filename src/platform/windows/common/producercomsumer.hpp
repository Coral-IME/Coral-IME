#pragma once

#include <condition_variable>
#include <mutex>
#include <queue>
#include <atomic>

template<typename T>
class CProducerAndComsumer
{
public:
	static CProducerAndComsumer* create(int maxQueueSize)
	{
		return new CProducerAndComsumer(maxQueueSize);
	}

	void close()
	{
		m_bExit = true;
		m_notFull.notify_all();
		m_notEmpty.notify_all();
	}

	virtual ~CProducerAndComsumer()
	{
		ASSERT(m_bExit == true);
	}

	bool product(const T &v, bool onlyNewest = false)
	{
		std::unique_lock<std::mutex> locker(m_lock);
		if (onlyNewest && isFull())
		{
			std::queue<T> empty;
			std::swap(m_queue, empty);
			m_queue.push(v);
			m_notEmpty.notify_one();
			m_notFull.notify_all();
			return !m_bExit;
		}
		while (isFull())
		{
			m_notFull.wait(locker);
			if (m_bExit)
				break;
		}
		m_queue.push(v);
		m_notEmpty.notify_one();
		return !m_bExit;
	}

	bool consume(T& v, bool onlyNewest = false)
	{
		std::unique_lock<std::mutex> locker(m_lock);
		while (isEmpty())
		{
			m_notEmpty.wait(locker);
			if (m_bExit)
				break;
		}
		if (onlyNewest)
		{
			if (!m_queue.empty())
				v = m_queue.back();
			while (!m_queue.empty())
				m_queue.pop();
		}
		else
		{
			if (!m_queue.empty())
			{
				v = m_queue.front();
				m_queue.pop();
			}
		}
		m_notFull.notify_one();
		return !m_bExit;
	}

private:
	explicit CProducerAndComsumer(int maxQueueSize)
	{
		m_maxQueueSize = maxQueueSize;
		m_bExit = false;
	}

	bool isEmpty() const
	{
		return m_queue.empty();
	}

	bool isFull() const
	{
		return m_queue.size() == m_maxQueueSize;
	}

private:
	std::queue<T> m_queue;
	std::condition_variable_any m_notEmpty;
	std::condition_variable_any m_notFull;
	int m_maxQueueSize;
	std::mutex m_lock;
	std::atomic_bool m_bExit;
};