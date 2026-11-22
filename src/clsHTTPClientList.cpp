#include "clsHTTPClientList.h"
#include "clsHTTPClient.h"
#include <vector>

namespace rizweb {

void HTTPClientList::add(HTTPClient *client)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clients[client->id()] = client;
}

void HTTPClientList::remove(HTTPClient *client)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_clients.erase(client->id());
}

size_t HTTPClientList::count()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_clients.size();
}

bool HTTPClientList::post(uint32_t id, std::function<void(HTTPClient &)> task)
{
    NetReactor *reactor = nullptr;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        auto it = m_clients.find(id);
        if (it == m_clients.end())
            return false;
        reactor = it->second->getReactor();
    }
    reactor->post([this, id, task]() {
        HTTPClient *client = nullptr;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            auto it = m_clients.find(id);
            if (it != m_clients.end())
                client = it->second;
        }
        if (client)
            task(*client);      // we are on the client's reactor thread now
    });
    return true;
}

void HTTPClientList::postToAll(const std::function<void(HTTPClient &)> &task)
{
    std::vector<uint32_t> ids;
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        ids.reserve(m_clients.size());
        for (const auto &item : m_clients)
            ids.push_back(item.first);
    }
    for (uint32_t id : ids)
        post(id, task);
}

} // namespace rizweb
