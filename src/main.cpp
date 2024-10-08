#include <iostream>
#include <string>
#include <vector>
#include "MySharedPtr.h"
#include <concepts>
#include <unordered_map>
#include "Logger.h"
#include <random>
#include <algorithm>
#include <ranges>
#include <functional>
#include <cppcoro/task.hpp>
#include <cppcoro/when_all.hpp>
#include <cppcoro/sync_wait.hpp>
#include <fstream>
#include <iostream>

class Player;
class PlayerStrategy;

MySharedPtr<Player> getRandomPlayer(const std::vector<MySharedPtr<Player>>& candidates);


class PlayerStrategy {
public:
    virtual ~PlayerStrategy() = default;

    virtual cppcoro::task<std::string> vote(
        const std::vector<MySharedPtr<Player>>& players,
        const std::function<bool(const MySharedPtr<Player>&)> targetFilter) = 0;

    virtual cppcoro::task<std::pair<std::string, std::string>> chooseAction(
        const std::vector<MySharedPtr<Player>>& players, 
        const std::vector<std::string>& availableActions,
        const std::function<bool(const MySharedPtr<Player>&)> targetFilter) = 0;
};

class Player {
public:
    Player(const std::string& name, MySharedPtr<PlayerStrategy> strategy) 
        : playerName(name), alive(true), strategy(strategy) {}

    virtual ~Player() = default;

    virtual cppcoro::task<std::string> vote(const std::vector<MySharedPtr<Player>>& players) {        
        // не голосуем против себя
        auto targetFilter = [this](const MySharedPtr<Player>& player) {
            return player->isAlive() && player.get() != this;
        };

        return strategy->vote(players, targetFilter);
    }

    virtual cppcoro::task<std::pair<std::string, std::string>> nightAction(
        const std::vector<MySharedPtr<Player>>& players) = 0;

    std::string getName() const { return playerName; }
    bool isAlive() const { return alive; }
    void die() { alive = false; }
    MySharedPtr<PlayerStrategy> getStrategy() const { return strategy; }

protected:
    std::string playerName;
    bool alive;
    MySharedPtr<PlayerStrategy> strategy;
};

class BotStrategy : public PlayerStrategy {
public:
    cppcoro::task<std::string> vote(
        const std::vector<MySharedPtr<Player>>& players,
        const std::function<bool(const MySharedPtr<Player>&)> targetFilter) override {
        
        auto potentialTargets = players | std::ranges::views::filter(targetFilter);
        std::vector<MySharedPtr<Player>> filteredPlayers(potentialTargets.begin(), potentialTargets.end());

        auto target = getRandomPlayer(filteredPlayers);
        if (target) {
            co_return target->getName();
        }
        
        co_return "";
    }


    cppcoro::task<std::pair<std::string, std::string>> chooseAction(
        const std::vector<MySharedPtr<Player>>& players, 
        const std::vector<std::string>& availableActions,
        const std::function<bool(const MySharedPtr<Player>&)> targetFilter) override {
        
        auto potentialTargets = players 
            | std::ranges::views::filter(targetFilter);
        std::vector<MySharedPtr<Player>> filteredPlayers(potentialTargets.begin(), potentialTargets.end());

        auto target = getRandomPlayer(filteredPlayers);
        if (target && !availableActions.empty()) {
            std::string action = availableActions[rand() % availableActions.size()];
            co_return std::make_pair(action, target->getName());
        }
        co_return std::make_pair("", "");
    }
};

class UserStrategy : public PlayerStrategy {
public:
    cppcoro::task<std::string> vote(
        const std::vector<MySharedPtr<Player>>& players,
        const std::function<bool(const MySharedPtr<Player>&)> targetFilter) override {
        
        std::cout << "Введите имя игрока, за которого хотите проголосовать: ";
        std::string choice;
        std::cin >> choice;

        auto it = std::find_if(players.begin(), players.end(), [&](const MySharedPtr<Player>& player) {
            return player->getName() == choice && targetFilter(player);
        });

        if (it != players.end()) {
            co_return choice;
        }
        co_return "";
    }

    cppcoro::task<std::pair<std::string, std::string>> chooseAction(
        const std::vector<MySharedPtr<Player>>& players, 
        const std::vector<std::string>& availableActions,
        const std::function<bool(const MySharedPtr<Player>&)> targetFilter) override {
        
        std::cout << "Введите имя игрока, с которым хотите совершить действие: ";
        std::string target;
        std::cin >> target;

        std::cout << "Доступные действия:\n";
        for (const auto& action : availableActions) {
            std::cout << "- " << action << "\n";
        }
        std::cout << "Введите действие: ";
        std::string action;
        std::cin >> action;

        auto it = std::find_if(players.begin(), players.end(), [&](const MySharedPtr<Player>& player) {
            return player->getName() == target && targetFilter(player);
        });

        if (it != players.end() && std::find(availableActions.begin(), availableActions.end(), action) != availableActions.end()) {
            co_return std::make_pair(action, target);
        }
        co_return std::make_pair("", "");
    }
};


std::vector<std::string> loadNames(const std::string& fileName) {
    std::ifstream file(fileName);
    std::vector<std::string> names;
    std::string name;

    if (!file) {
        std::cerr << "Не удалось открыть файл с именами.\n";
        return names;
    }

    while (std::getline(file, name)) {
        if (!name.empty()) {
            names.push_back(name);
        }
    }

    file.close();
    return names;
}

MySharedPtr<Player> getRandomPlayer(const std::vector<MySharedPtr<Player>>& candidates) {
    if (candidates.empty()) {
        return nullptr;
    }

    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> distr(0, candidates.size() - 1);

    return candidates[distr(gen)];
}

class Doctor : public Player {
public:
    Doctor(const std::string& name, MySharedPtr<PlayerStrategy> strategy)
        : Player(name, strategy), lastHealed("") {}

    cppcoro::task<std::pair<std::string, std::string>> nightAction(const std::vector<MySharedPtr<Player>>& players) override {
        std::vector<std::string> actions = {"heal"};

        auto targetFilter = [this](const MySharedPtr<Player>& player) {
            return player->isAlive() && player->getName() != lastHealed;
        };

        auto [action, target] = co_await strategy->chooseAction(players, actions, targetFilter);
        if (action == "heal") {
            lastHealed = target;
        }
        co_return std::make_pair(action, target);
    }

private:
    std::string lastHealed;  // не лечим одного и того же игрока два раза подряд
};


class Mafia : public Player {
public:
    Mafia(const std::string& name, MySharedPtr<PlayerStrategy> strategy)
        : Player(name, strategy) {}

    cppcoro::task<std::pair<std::string, std::string>> nightAction(const std::vector<MySharedPtr<Player>>& players) override {
        std::vector<std::string> actions = {"kill"};

        auto targetFilter = [this](const MySharedPtr<Player>& player) {
            return player->isAlive() && !isAlliedWith(player);
        };

        auto [action, target] = co_await strategy->chooseAction(players, actions, targetFilter);
        co_return std::make_pair(action, target);
    }

    cppcoro::task<std::string> vote(const std::vector<MySharedPtr<Player>>& players) override {
        // мафия не голосует против мафии
        auto targetFilter = [this](const MySharedPtr<Player>& player) {
            return player->isAlive() && !isAlliedWith(player) && player.get() != this;
        };

        return strategy->vote(players, targetFilter);
    }

protected:
    bool isAlliedWith(const MySharedPtr<Player>& player) const {
        return dynamic_cast<Mafia*>(player.get()) != nullptr;
    }
};

class Bull : public Mafia {
public:
    Bull(const std::string& name, MySharedPtr<PlayerStrategy> strategy)
        : Mafia(name, strategy) {}
};

class Ninja : public Mafia {
public:
    Ninja(const std::string& name, MySharedPtr<PlayerStrategy> strategy)
        : Mafia(name, strategy) {}
};

class Killer : public Mafia {
public:
    Killer(const std::string& name, MySharedPtr<PlayerStrategy> strategy)
        : Mafia(name, strategy) {}
};

class Civilian : public Player {
public:
    Civilian(const std::string& name, MySharedPtr<PlayerStrategy> strategy)
        : Player(name, strategy) {}

    cppcoro::task<std::pair<std::string, std::string>> nightAction(const std::vector<MySharedPtr<Player>>& players) override {
        // мирный житель ночью ничего не делает
        co_return std::make_pair("", "");
    }
};


class Maniac : public Player {
public:
    Maniac(const std::string& name, MySharedPtr<PlayerStrategy> strategy)
        : Player(name, strategy) {}

    cppcoro::task<std::pair<std::string, std::string>> nightAction(const std::vector<MySharedPtr<Player>>& players) override {
        std::vector<std::string> actions = {"kill"};

        auto targetFilter = [this](const MySharedPtr<Player>& player) {
            return player->isAlive() && player.get() != this;
        };

        auto [action, target] = co_await strategy->chooseAction(players, actions, targetFilter);
        co_return std::make_pair(action, target);
    }
};


class Commissar : public Player {
public:
    Commissar(const std::string& name, MySharedPtr<PlayerStrategy> strategy)
        : Player(name, strategy) {}

    void addCheckedPlayer(const std::string& playerName, bool isMafia) {
        checkedPlayers[playerName] = isMafia;
    }

    cppcoro::task<std::pair<std::string, std::string>> nightAction(const std::vector<MySharedPtr<Player>>& players) override {
        std::vector<std::string> actions = {"check", "kill"};

        // комиссар может сделать действие над всеми, кроме себя и проверенных мирных
        auto targetFilter = [this](const MySharedPtr<Player>& player) {
            return player->isAlive() && player.get() != this && !isCheckedAndInnocent(player->getName());
        };

        auto [action, target] = co_await strategy->chooseAction(players, actions, targetFilter);
        co_return std::make_pair(action, target);
    }

    // не голосует против проверенных мирных
    cppcoro::task<std::string> vote(const std::vector<MySharedPtr<Player>>& players) override {
        auto targetFilter = [this](const MySharedPtr<Player>& player) {
            return player->isAlive() && player.get() != this && !isCheckedAndInnocent(player->getName());
        };

        return strategy->vote(players, targetFilter);
    }

private:
    // имя игрока и статус (true — мафия, false — мирный)
    std::unordered_map<std::string, bool> checkedPlayers;

    bool isCheckedAndInnocent(const std::string& playerName) const {
        auto it = checkedPlayers.find(playerName);
        return it != checkedPlayers.end() && !it->second;
    }
};



class GameMaster {
public:
    GameMaster(int numPlayers, bool isUserPlayer)
        : numPlayers(numPlayers), isUserPlayer(isUserPlayer), currentDay(1) {
        assignRoles();
    }

    void runGame() {
        while (!isGameOver()) {
            playDayPhase();
            if (isGameOver()) break;
            
            playNightPhase();
            announceNightResults();
            ++currentDay;
        }
    }

private:
    int numPlayers;
    bool isUserPlayer;
    int currentDay;
    std::vector<MySharedPtr<Player>> players;
    std::vector<std::string> playersToReveal;
    std::vector<std::string> healedPlayers;

    Logger logger;



    void assignRandomRole(const std::string& playerName, int& numMafia, int& numDoctors, int& numCommissars, int& numManiacs, int& numCivilians, 
                      std::vector<std::string>& mafiaNames, bool& bullAssigned, bool& ninjaAssigned, bool& killerAssigned) {
    int randomRole = std::rand() % (numMafia + numDoctors + numCommissars + numManiacs + numCivilians);
    std::string assignedRole;
    
    if (randomRole < numMafia) {
        int mafiaType = std::rand() % 4;
        
        if (mafiaType == 0 || (bullAssigned && ninjaAssigned && killerAssigned)) {
            players.push_back(MySharedPtr<Mafia>(new Mafia(playerName, MySharedPtr<BotStrategy>(new BotStrategy()))));
            assignedRole = "мафия";
        } else if (mafiaType == 1 && !bullAssigned) {
            players.push_back(MySharedPtr<Bull>(new Bull(playerName, MySharedPtr<BotStrategy>(new BotStrategy()))));
            assignedRole = "бык";
            bullAssigned = true; 
        } else if (mafiaType == 2 && !ninjaAssigned) {
            players.push_back(MySharedPtr<Ninja>(new Ninja(playerName, MySharedPtr<BotStrategy>(new BotStrategy()))));
            assignedRole = "ниндзя";
            ninjaAssigned = true; 
        } else if (mafiaType == 3 && !killerAssigned) {
            players.push_back(MySharedPtr<Killer>(new Killer(playerName, MySharedPtr<BotStrategy>(new BotStrategy()))));
            assignedRole = "киллер";
            killerAssigned = true; 
        } else {
            players.push_back(MySharedPtr<Mafia>(new Mafia(playerName, MySharedPtr<BotStrategy>(new BotStrategy()))));
            assignedRole = "мафия";
        }
        
        mafiaNames.push_back(playerName);
        numMafia--;
    } else if (randomRole < numMafia + numDoctors) {
        players.push_back(MySharedPtr<Doctor>(new Doctor(playerName, MySharedPtr<BotStrategy>(new BotStrategy()))));
        assignedRole = "доктор";
        numDoctors--;
    } else if (randomRole < numMafia + numDoctors + numCommissars) {
        players.push_back(MySharedPtr<Commissar>(new Commissar(playerName, MySharedPtr<BotStrategy>(new BotStrategy()))));
        assignedRole = "комиссар";
        numCommissars--;
    } else if (randomRole < numMafia + numDoctors + numCommissars + numManiacs) {
        players.push_back(MySharedPtr<Maniac>(new Maniac(playerName, MySharedPtr<BotStrategy>(new BotStrategy()))));
        assignedRole = "маньяк";
        numManiacs--;
    } else {
        players.push_back(MySharedPtr<Civilian>(new Civilian(playerName, MySharedPtr<BotStrategy>(new BotStrategy()))));
        assignedRole = "мирный житель";
        numCivilians--;
    }

    logger.logDayAction(0, playerName + " получил роль: " + assignedRole);
}



void assignRoles() {
    std::vector<std::string> names = loadNames("../names.txt");

    if (names.size() < numPlayers) {
        std::cerr << "\n*** Недостаточно имен в файле для игры. Минимум " << numPlayers << ". ***\n";
        return;
    }

    std::shuffle(names.begin(), names.end(), std::mt19937(std::random_device()()));

    int numMafia = std::max(1, numPlayers / 5);
    int numDoctors = 1;
    int numCommissars = 1;
    int numManiacs = 1;
    int numCivilians = numPlayers - numMafia - numDoctors - numCommissars - numManiacs;

    std::vector<std::string> mafiaNames;

    bool bullAssigned = false;
    bool ninjaAssigned = false;
    bool killerAssigned = false;

    if (isUserPlayer) {
        std::string playerName;
        std::cout << "Введите свое имя: ";
        std::cin >> playerName;
        std::cout << "Выберите роль (mafia, bull, ninja, killer, doctor, commissar, maniac, civilian) или нажмите Enter для случайного выбора: ";
        std::cin.ignore(std::numeric_limits<std::streamsize>::max(), '\n');
        std::string role;
        std::getline(std::cin, role);

        std::string assignedRole;

        if (role == "mafia") {
            players.push_back(MySharedPtr<Mafia>(new Mafia(playerName, MySharedPtr<UserStrategy>(new UserStrategy()))));
            mafiaNames.push_back(playerName);
            assignedRole = "мафия";
            numMafia--;
        } else if (role == "bull" && !bullAssigned) {
            players.push_back(MySharedPtr<Bull>(new Bull(playerName, MySharedPtr<UserStrategy>(new UserStrategy()))));
            mafiaNames.push_back(playerName);
            assignedRole = "бык";
            bullAssigned = true;
            numMafia--;
        } else if (role == "ninja" && !ninjaAssigned) {
            players.push_back(MySharedPtr<Ninja>(new Ninja(playerName, MySharedPtr<UserStrategy>(new UserStrategy()))));
            mafiaNames.push_back(playerName);
            assignedRole = "ниндзя";
            ninjaAssigned = true;
            numMafia--;
        } else if (role == "killer" && !killerAssigned) {
            players.push_back(MySharedPtr<Killer>(new Killer(playerName, MySharedPtr<UserStrategy>(new UserStrategy()))));
            mafiaNames.push_back(playerName);
            assignedRole = "киллер";
            killerAssigned = true;
            numMafia--;
        } else if (role == "doctor") {
            players.push_back(MySharedPtr<Doctor>(new Doctor(playerName, MySharedPtr<UserStrategy>(new UserStrategy()))));
            assignedRole = "доктор";
            numDoctors--;
        } else if (role == "commissar") {
            players.push_back(MySharedPtr<Commissar>(new Commissar(playerName, MySharedPtr<UserStrategy>(new UserStrategy()))));
            assignedRole = "комиссар";
            numCommissars--;
        } else if (role == "maniac") {
            players.push_back(MySharedPtr<Maniac>(new Maniac(playerName, MySharedPtr<UserStrategy>(new UserStrategy()))));
            assignedRole = "маньяк";
            numManiacs--;
        } else if (role == "civilian") {
            players.push_back(MySharedPtr<Civilian>(new Civilian(playerName, MySharedPtr<UserStrategy>(new UserStrategy()))));
            assignedRole = "мирный житель";
            numCivilians--;
        } else {
            
            assignRandomRole(playerName, numMafia, numDoctors, numCommissars, numManiacs, numCivilians, mafiaNames, bullAssigned, ninjaAssigned, killerAssigned);
        }

        
        logger.logDayAction(0, playerName + " получил роль: " + assignedRole);

        
        names.erase(std::remove(names.begin(), names.end(), playerName), names.end());
    }

    
    for (const auto& name : names) {
        assignRandomRole(name, numMafia, numDoctors, numCommissars, numManiacs, numCivilians, mafiaNames, bullAssigned, ninjaAssigned, killerAssigned);
        if (players.size() == numPlayers) break;
    }

    
    if (isUserPlayer && std::find(mafiaNames.begin(), mafiaNames.end(), players.front()->getName()) != mafiaNames.end()) {
        std::cout << "\nВы — мафиози! Вот список всех мафиози:\n";
        for (const auto& name : mafiaNames) {
            if (name != players.front()->getName()) {
                std::cout << "- " << name << "\n";
            }
        }
    }

    std::cout << "\n========== ИГРОКИ В ЭТОЙ ИГРЕ ==========\n";
    for (const auto& player : players) {
        std::cout << "- " << player->getName() << std::endl;
    }
    std::cout << "=========================================\n" << std::endl;
}
    

    void addPlayerToReveal(const std::string& playerName) {
        if (std::find(playersToReveal.begin(), playersToReveal.end(), playerName) == playersToReveal.end()) {
            playersToReveal.push_back(playerName);
        }
    }

  void playNightPhase() {
        std::string mafiaVictim, killerVictim, maniacVictim, doctorHeal, commissarAction, commissarTarget;
        MySharedPtr<Player> commissarPlayer;
        std::unordered_map<std::string, int> mafiaVotes;

        std::string logMessage = "НОЧЬ " + std::to_string(currentDay) + " НАСТУПИЛА. Начались ночные действия.\n";

        std::vector<cppcoro::task<std::pair<std::string, std::string>>> nightTasks;
        std::vector<MySharedPtr<Player>> alivePlayers;

        for (auto& player : players | std::ranges::views::filter([](const MySharedPtr<Player>& p) { return p->isAlive(); })) {
            alivePlayers.push_back(player);
            nightTasks.push_back(player->nightAction(players));
        }

        auto results = cppcoro::sync_wait(cppcoro::when_all(std::move(nightTasks)));

        for (size_t i = 0; i < alivePlayers.size(); ++i) {
            const auto& action = results[i];
            const std::string& actionType = action.first;
            const std::string& target = action.second;
            auto currentPlayer = alivePlayers[i];

            if (!actionType.empty() && !target.empty()) {
                logMessage += currentPlayer->getName() + " совершает действие: " + actionType + " на " + target + ".\n";
            }

            if (actionType == "kill") {
                if (dynamic_cast<Mafia*>(currentPlayer.get()) && !dynamic_cast<Killer*>(currentPlayer.get())) {
                    mafiaVotes[target]++;
                } else if (dynamic_cast<Killer*>(currentPlayer.get())) {
                    killerVictim = target;
                } else if (dynamic_cast<Maniac*>(currentPlayer.get())) {
                    auto victim = findPlayerByName(target);
                    if (victim && dynamic_cast<Bull*>(victim.get())) {
                        logMessage += "Маньяк попытался убить " + target + ", но это был Бык, и он не был убит.\n";
                    } else {
                        maniacVictim = target;
                    }
                } else if (dynamic_cast<Commissar*>(currentPlayer.get())) {
                    commissarPlayer = currentPlayer;
                    commissarAction = actionType;
                    commissarTarget = target;
                }
            } else if (actionType == "heal" && dynamic_cast<Doctor*>(currentPlayer.get())) {
                doctorHeal = target;
            } else if (actionType == "check" && dynamic_cast<Commissar*>(currentPlayer.get())) {
                commissarPlayer = currentPlayer;
                commissarAction = actionType;
                commissarTarget = target;
            }
        }

        int maxVotes = 0;
        for (const auto& [name, count] : mafiaVotes) {
            if (count > maxVotes) {
                maxVotes = count;
                mafiaVictim = name;
            } else if (count == maxVotes) {
                if (std::rand() % 2 == 0) {
                    mafiaVictim = name;
                }
            }
        }

        if (!mafiaVictim.empty()) {
            logMessage += "Мафия выбрала жертву: " + mafiaVictim + ".\n";
        }
        if (!killerVictim.empty()) {
            logMessage += "Киллер выбрал жертву: " + killerVictim + ".\n";
        }
        if (!doctorHeal.empty()) {
            if (mafiaVictim == doctorHeal || killerVictim == doctorHeal || maniacVictim == doctorHeal) {
                healedPlayers.push_back(doctorHeal);
            }
            logMessage += "Доктор лечит: " + doctorHeal + ".\n";
        }

        if (!maniacVictim.empty()) {
            logMessage += "Маньяк выбрал жертву: " + maniacVictim + ".\n";
        }

        if (!mafiaVictim.empty() && mafiaVictim != doctorHeal) {
            killPlayer(mafiaVictim);
            addPlayerToReveal(mafiaVictim);
            logMessage += "Мафия убила: " + mafiaVictim + ".\n";
        }
        if (!killerVictim.empty() && killerVictim != doctorHeal) {
            killPlayer(killerVictim);
            addPlayerToReveal(killerVictim);
            logMessage += "Киллер убил: " + killerVictim + ".\n";
        }

        if (!maniacVictim.empty() && maniacVictim != doctorHeal) {
            killPlayer(maniacVictim);
            addPlayerToReveal(maniacVictim);
            logMessage += "Маньяк убил: " + maniacVictim + ".\n";
        }

        
        if (!commissarTarget.empty()) {
            if (commissarAction == "kill" && commissarTarget != doctorHeal) {
                killPlayer(commissarTarget);
                addPlayerToReveal(commissarTarget);
                logMessage += "Комиссар убил: " + commissarTarget + ".\n";
            } else if (commissarAction == "check") {
                auto targetPlayer = findPlayerByName(commissarTarget);
                if (targetPlayer) {
                        bool isMafia = dynamic_cast<Mafia*>(targetPlayer.get()) != nullptr && dynamic_cast<Ninja*>(targetPlayer.get()) == nullptr;                    if (Commissar* commissar = dynamic_cast<Commissar*>(commissarPlayer.get())) {
                        commissar->addCheckedPlayer(commissarTarget, isMafia);
                        
                        logMessage += "Комиссар проверил: " + commissarTarget + ". Это " + (isMafia ? "мафия." : "не мафия.") + "\n";

                        if (dynamic_cast<UserStrategy*>(commissar->getStrategy().get())) {
                            std::cout << "\nРезультат проверки: " << commissarTarget << " — ";
                            if (isMafia) {
                                std::cout << "мафия." << std::endl;
                            } else {
                                std::cout << "не мафия." << std::endl;
                            }
                        }
                    }
                }
            }
        }
        logger.logNightAction(currentDay, logMessage);
    }





    MySharedPtr<Player> findPlayerByName(const std::string& name) {
        auto it = std::find_if(players.begin(), players.end(), [&](const MySharedPtr<Player>& player) {
            return player->getName() == name;
        });
        return (it != players.end()) ? *it : nullptr;
    }

    void killPlayer(const std::string& name) {
        auto player = findPlayerByName(name);
        if (player) {
            player->die();
        }
    }

    void announceNightResults() {
        std::cout << "\n========== РЕЗУЛЬТАТЫ НОЧИ ==========\n";
        for (const auto& playerName : playersToReveal) {
            auto player = findPlayerByName(playerName);
            if (player) {
                std::cout << "\n*** " << playerName << " был убит прошлой ночью. Он был ";
                if (dynamic_cast<Mafia*>(player.get())) {
                    std::cout << "мафией. ***\n";
                } else if (dynamic_cast<Doctor*>(player.get())) {
                    std::cout << "доктором. ***\n";
                } else if (dynamic_cast<Commissar*>(player.get())) {
                    std::cout << "комиссаром. ***\n";
                } else if (dynamic_cast<Maniac*>(player.get())) {
                    std::cout << "маньяком. ***\n";
                } else {
                    std::cout << "мирным жителем. ***\n";
                }
            }
        }

        for (const auto& playerName : healedPlayers) {
            std::cout << "\n*** " << playerName << " был спасен прошлой ночью доктором. ***\n";
        }
        std::cout << "======================================\n" << std::endl;
        playersToReveal.clear();
        healedPlayers.clear();
    }

   bool isGameOver() {
    int numMafia = std::ranges::count_if(players, [](const MySharedPtr<Player>& player) {
        return player->isAlive() && dynamic_cast<Mafia*>(player.get()) != nullptr;
    });

    int numCivilians = std::ranges::count_if(players, [](const MySharedPtr<Player>& player) {
        return player->isAlive() && !dynamic_cast<Mafia*>(player.get()) && !dynamic_cast<Maniac*>(player.get());
    });

    int numManiac = std::ranges::count_if(players, [](const MySharedPtr<Player>& player) {
        return player->isAlive() && dynamic_cast<Maniac*>(player.get()) != nullptr;
    });

    std::string logMessage = "РЕЗУЛЬТАТЫ ИГРЫ:\n";

    if (numMafia > numCivilians) {
        std::cout << "\n*** Мафия победила! Количество мафов больше количества мирных жителей. ***\n";
        std::cout << "Осталось:\n"
                  << "- Мафия: " << numMafia << "\n"
                  << "- Мирные жители: " << numCivilians << "\n";

        logMessage += "Мафия победила. Количество мафов больше количества мирных жителей.\n";
        logMessage += "Остаток мафии: " + std::to_string(numMafia) + "\nОстаток мирных жителей: " + std::to_string(numCivilians) + "\n";
        logFinalResult(logMessage);
        return true;
    }

    if (numMafia == numCivilians && numManiac == 0) {
        std::cout << "\n*** Мафия победила! Количество мафов равно количеству мирных жителей. ***\n";
        std::cout << "Осталось:\n"
                  << "- Мафия: " << numMafia << "\n"
                  << "- Мирные жители: " << numCivilians << "\n";

        logMessage += "Мафия победила. Количество мафов равно количеству мирных жителей.\n";
        logMessage += "Остаток мафии: " + std::to_string(numMafia) + "\nОстаток мирных жителей: " + std::to_string(numCivilians) + "\n";
        logFinalResult(logMessage);
        return true;
    }

    if (numMafia == 0 && numManiac == 0) {
        std::cout << "\n*** Мирные жители победили! Все мафы и маньяк убиты. ***\n";
        std::cout << "Осталось:\n"
                  << "- Мирные жители: " << numCivilians << "\n";

        logMessage += "Мирные жители победили. Все мафы и маньяк убиты.\n";
        logMessage += "Остаток мирных жителей: " + std::to_string(numCivilians) + "\n";
        logFinalResult(logMessage);
        return true;
    }

    if (numManiac == 1 && numMafia == 0 && numCivilians == 1) {
        std::cout << "\n*** Маньяк победил! Он остался один на один с мирным жителем. ***\n";
        std::cout << "Осталось:\n"
                  << "- Маньяк: 1\n"
                  << "- Мирные жители: 1\n";

        logMessage += "Маньяк победил. Он остался один на один с мирным жителем.\n";
        logMessage += "Остаток маньяка: 1\nОстаток мирных жителей: 1\n";
        logFinalResult(logMessage);
        return true;
    }

    return false;
}

void logFinalResult(const std::string& logMessage) {
    std::string finalLog = logMessage + "СОСТОЯНИЕ ИГРОКОВ:\n";
    for (const auto& player : players) {
        std::string role;

        if (dynamic_cast<Mafia*>(player.get())) {
            role = "Мафия";
        } else if (dynamic_cast<Doctor*>(player.get())) {
            role = "Доктор";
        } else if (dynamic_cast<Commissar*>(player.get())) {
            role = "Комиссар";
        } else if (dynamic_cast<Maniac*>(player.get())) {
            role = "Маньяк";
        } else if (dynamic_cast<Civilian*>(player.get())) {
            role = "Мирный житель";
        }

        finalLog += "Имя: " + player->getName() + ", Роль: " + role + ", Статус: " + (player->isAlive() ? "Жив" : "Мертв") + "\n";
    }

    finalLog += "=====================================\n";
    logger.logResult(finalLog);
}


   void playDayPhase() {
    std::cout << "\n********** ДЕНЬ " << currentDay << " НАСТУПИЛ **********\n";
    std::unordered_map<std::string, int> voteCount;
    std::unordered_map<std::string, std::string> playerVotes;

    std::vector<cppcoro::task<std::string>> voteTasks;
    for (auto& player : players | std::ranges::views::filter([](const MySharedPtr<Player>& p) { return p->isAlive(); })) {
        voteTasks.push_back(player->vote(players));
    }

    auto results = cppcoro::sync_wait(cppcoro::when_all(std::move(voteTasks)));

    std::string logMessage = "ДЕНЬ " + std::to_string(currentDay) + " НАСТУПИЛ. Началось голосование.\n";

    int index = 0;
    for (auto& player : players | std::ranges::views::filter([](const MySharedPtr<Player>& p) { return p->isAlive(); })) {
        const auto& target = results[index++];
        if (!target.empty()) {
            voteCount[target]++;
            playerVotes[player->getName()] = target;
            logMessage += "Игрок " + player->getName() + " голосует за " + target + ".\n";
        }
    }

    std::cout << "\n========== ДНЕВНОЕ ГОЛОСОВАНИЕ ==========\n";
    for (const auto& [voter, target] : playerVotes) {
        std::cout << voter << " голосует за " << target << "\n";
    }
    std::cout << "------------------------------------------\n";
    
    std::cout << "РЕЗУЛЬТАТЫ ГОЛОСОВАНИЯ:\n";
    for (const auto& [name, count] : voteCount) {
        std::cout << name << ": " << count << "\n";
        logMessage += name + " получил " + std::to_string(count) + " голосов.\n";
    }
    std::cout << "==========================================\n" << std::endl;

    std::string eliminatedPlayer;
    int maxVotes = 0;
    for (const auto& [name, count] : voteCount) {
        if (count > maxVotes) {
            maxVotes = count;
            eliminatedPlayer = name;
        } else if (count == maxVotes) {
            if (std::rand() % 2 == 0) {
                eliminatedPlayer = name;
            }
        }
    }

    if (!eliminatedPlayer.empty()) {
        auto it = std::find_if(players.begin(), players.end(), [&](const MySharedPtr<Player>& p) {
            return p->getName() == eliminatedPlayer;
        });

        if (it != players.end()) {
            (*it)->die();
            std::cout << "*** " << eliminatedPlayer << " был казнен днем. ***\n";

            if (dynamic_cast<Mafia*>((*it).get())) {
                std::cout << "*** Он был мафией. ***\n";
                logMessage += "\nИгрок " + eliminatedPlayer + " был казнен и он был мафией.\n";
            } else {
                std::cout << "*** Он был не мафией. ***\n";
                logMessage += "\nИгрок " + eliminatedPlayer + " был казнен и он был не мафией.\n";
            }
            
            logMessage += "\nИгрок " + eliminatedPlayer + " был исключен с " + std::to_string(maxVotes) + " голосами.\n";
        }
    } else {
        logMessage += "\nНикто не был исключен.\n";
    }

    logger.logDayAction(currentDay, logMessage);

    std::cout << "**************************************\n";
}



};


int main() {
    int numPlayers;
    char userChoice;

    std::cout << "Введите количество игроков (минимум 5): ";
    std::cin >> numPlayers;

    if (numPlayers < 5) {
        std::cerr << "Недостаточно игроков для игры. Минимум 5.\n";
        return 1;
    }

    std::cout << "Вы хотите участвовать в игре? (y/n): ";
    std::cin >> userChoice;

    bool isUserPlayer = (userChoice == 'y' || userChoice == 'Y');

    GameMaster gameMaster(numPlayers, isUserPlayer);
    gameMaster.runGame();

    return 0;
}