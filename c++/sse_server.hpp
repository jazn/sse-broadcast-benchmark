#include <map>
#include <atomic>
#include <list>
#include <string>
#include <boost/asio.hpp>
#include <mutex>
#include "http_handler.hpp"
#include "sse_client.hpp"

using boost::asio::ip::tcp;

template<class T>
struct node {
  node() : data(nullptr), next(nullptr) {}
  node(const T& data) : data(data), next(nullptr) {}
  node* next;
  T data;
};

template<class T>
class singlylist {
public:
  singlylist() {
    _head.store(nullptr);
  }
  
  void push(const T& data) {
    node<T>* new_node = new node<T>(data);
    //new_node->next = head.load(std::memory_order_relaxed);
    //while (
      //!std::atomic_compare_exchange_weak_explicit(
        //&head, &new_node->next,
        //new_node,
        //std::memory_order_release,
        //std::memory_order_relaxed
      //));
    // bug workaround for gcc < 4.8.3 follows ..
    node<T>* old_head = _head.load(std::memory_order_relaxed);
    do {
      new_node->next = old_head;
    } while (!_head.compare_exchange_weak(old_head, new_node,
                                          std::memory_order_release,
                                          std::memory_order_relaxed));
  }

  void lock() {
    _mutex.lock();
  }

  void unlock() {
    _mutex.unlock();
  }

  void remove(node<T>* remove_node) {
    node<T>* iterator = _head.load(std::memory_order_relaxed);
    node<T>* prev = nullptr;
    if (iterator == remove_node) {
      // catch special case for first element
      // replace the head with whatever next points to (nullptr or value)
      if (_head.compare_exchange_weak(iterator, iterator->next, std::memory_order_release, std::memory_order_relaxed)) {
        delete remove_node;
        return;
      }
      // else: another thread raced us to insert a new head, so move along
    }
    // process the other (not head) elements
    prev = iterator;
    iterator = iterator->next;
    while (iterator != nullptr) {
      if (iterator == remove_node) {
        prev->next = iterator->next;
        iterator->data = nullptr;
        delete remove_node;
        break;
      }
      prev = iterator;
      iterator = iterator->next;
    }
  }

  node<T>* get_head() {
    return _head.load(std::memory_order_relaxed); 
  }

private:
  std::atomic<node<T>*> _head;
  std::mutex _mutex;
};

class sse_server {
public:
  sse_server(boost::asio::io_service& io_service, unsigned short port)
    : _acceptor(io_service, tcp::endpoint(tcp::v4(), port)),
      _socket(io_service)
  {
    init_handlers();
    do_accept();
  }

  void broadcast(const std::string& msg);

private:
  typedef std::function<void(boost::system::error_code, std::size_t)> write_cb;

  void do_accept();
  void write(std::shared_ptr<tcp::socket>& socket, const std::string& msg, write_cb cb);
  void init_handlers();

  int _sse_client_count = 0;
  //std::vector<std::shared_ptr<sse_client_bucket>> _sse_client_buckets;
  singlylist<std::shared_ptr<sse_client>> _sse_clients;
  std::shared_ptr<http_handler::handler_map> _handlers;
  tcp::acceptor _acceptor;
  tcp::socket _socket;
};
