//
// Created by mguzek on 15.05.2020.
//

#ifndef SCALABLE_FAIR_READER_WRITER_LOCK_READERWRITERLOCK_H
#define SCALABLE_FAIR_READER_WRITER_LOCK_READERWRITERLOCK_H

#include <atomic>
//#include <threads.h>
#include <cstdint>
#include "SNZI.h"

class ReaderWriterLock {
public:
    struct Node {
        enum class LockType { READER, WRITER };
        std::atomic<LockType> type;
        std::atomic<bool> locked;
        std::atomic<Node*> next;
        bool waitForNext;               //field only for readers

        Node() : waitForNext{false} {
            next.store(nullptr);
            //locked.store(false);		        //just to avoid CDSChecker's warnings regarding uninitialized atomics
            //type.store(LockType::READER);	//same as above
        }
    };

    class ReaderLock {
        ReaderWriterLock* const enclosingObject;

    public:
        explicit ReaderLock(ReaderWriterLock* owner) : enclosingObject{owner} {

        }

        void lock(const unsigned threadID, Node* myNode) {
            //enclosingObject->readers_count.fetch_add(1);
            enclosingObject->snzi.arrive(threadID);
            if(enclosingObject->tail.load() == nullptr) {
                return;
            }

            myNode->type.store(Node::LockType::READER);

            Node* pred = enclosingObject->tail.exchange(myNode);
            Node* copyForCAS = myNode;
            if(pred == nullptr && enclosingObject->tail.compare_exchange_strong(copyForCAS, nullptr)) {
                return;
            }

            if(pred != nullptr) {
                //enclosingObject->readers_count.fetch_sub(1);
                enclosingObject->snzi.depart(threadID);
                myNode->locked.store(true);
                pred->next.store(myNode);
                while(myNode->locked.load()) { //sort of unfortunate chain of loops for chain of READERS
                    //thrd_yield();
                }
                //enclosingObject->readers_count.fetch_add(1);
                enclosingObject->snzi.arrive(threadID);
            }

            if(myNode->next.load() == nullptr) {
                copyForCAS = myNode;
                if(enclosingObject->tail.load() == nullptr || enclosingObject->tail.compare_exchange_strong(copyForCAS, nullptr)) {
                    return;
                }
                while(myNode->next.load() == nullptr) {
                    //thrd_yield();
                }
            }

            myNode->waitForNext = true;
            if(myNode->next.load()->type.load() == Node::LockType::READER) {
                myNode->next.load()->locked.store(false);
                myNode->waitForNext = false;
            }
        }

        void unlock(const unsigned threadID, Node* myNode) {
            //enclosingObject->readers_count.fetch_sub(1);
            enclosingObject->snzi.depart(threadID);

            if(myNode->waitForNext) {
                while(myNode->next.load() == nullptr) {
                    //thrd_yield();
                }
                myNode->next.load()->locked.store(false);
            }
            myNode->next.store(nullptr);
            myNode->waitForNext = false;
        }
    };

    class WriterLock {
        ReaderWriterLock* const enclosingObject;

    public:
        explicit WriterLock(ReaderWriterLock* owner) : enclosingObject{owner} {

        }

        void lock(const unsigned threadID, Node* myNode) {
            myNode->type.store(Node::LockType::WRITER);

            Node* pred = enclosingObject->tail.exchange(myNode);

            if(pred != nullptr) {
                myNode->locked.store(true);
                pred->next.store(myNode);
                while(myNode->locked.load()) {
                    //thrd_yield();
                }
            }

            //while(enclosingObject->readers_count.load() > 0) {
            while(enclosingObject->snzi.query()) {
            //thrd_yield();
            }
        }

        void unlock(const unsigned threadID, Node* myNode) {
            if(myNode->next.load() == nullptr) {
                Node* copyForCAS = myNode;
                if(enclosingObject->tail.compare_exchange_strong(copyForCAS, nullptr)) {
                    return;
                }

                while(myNode->next.load() == nullptr) {
                    //thrd_yield();
                }
            }

            myNode->next.load()->locked.store(false);
            myNode->next.store(nullptr);
        }
    };

private:
    std::atomic<Node*> tail;
    //std::atomic<uint64_t> readers_count; //TODO: change to SNZI
    SNZI snzi;
    ReaderLock reader_Lock;
    WriterLock writer_Lock;

public:
    ReaderWriterLock() : snzi{5}, reader_Lock{this}, writer_Lock{this} {
        tail.store(nullptr);
        //readers_count.store(0);
    }

    ReaderLock& readerLock() {
        return reader_Lock;
    }

    WriterLock& writerLock() {
        return writer_Lock;
    }
};

#endif //SCALABLE_FAIR_READER_WRITER_LOCK_READERWRITERLOCK_H