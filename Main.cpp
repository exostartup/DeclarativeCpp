
#include <functional>
#include <unordered_set>
#include <optional>

class PropertyBase;
class ReactionBase;


class ReactionBase {
protected:
	bool dirtImmune = false;
	std::unordered_set<PropertyBase*> triggeringProperties;

public:
	void addTriggeringProperty(PropertyBase* property);
	void unsubscribeFromTriggeringProperties();
	virtual void makeDirty() = 0;
};


template<typename T>
class ValueGuard{
	T& reference;
	T value;
public:
	ValueGuard(T& reference) : reference(reference), value(reference) {}

	~ValueGuard() {
		reference = value;
	}
};

class Reaction: public ReactionBase {
public:
	using Function = std::function<void()>;

	Function function;

public:
	Reaction(Function function) {
		this->function = function;

		if (Reaction::isDeferred) {
			Reaction::deferred.insert(this);
		}
		else {
			Reaction::DeferredGuard d{};
			execute();
		}
	}

private:
	virtual void makeDirty() override {
		if (dirtImmune)
			return;
		Reaction::deferred.insert(this);
	}

	void execute() {
		ValueGuard<ReactionBase*> g(Reaction::current);
		Reaction::current = this;

		unsubscribeFromTriggeringProperties();

		function();		
	}

public:
	inline static ReactionBase* current{};
	inline static bool isDeferred = false;
	inline static std::unordered_set<Reaction*> deferred{};

	class DeferredGuard {
		bool enabled = false;
	public:
		DeferredGuard() {
			if (!isDeferred) {
				isDeferred = true;
				enabled = true;
			}
		}

		~DeferredGuard() {
			if (!enabled) return;

			size_t maxIterations = 64;
			while (deferred.size() > 0 && maxIterations > 0) {
				std::vector<Reaction*> copyOfDeferred(deferred.begin(), deferred.end());
				for (auto reaction : copyOfDeferred) {
					deferred.erase(reaction);
					reaction->execute();
				}
				maxIterations--;
			}

			if (maxIterations == 0) {
				throw std::exception("Recursive property binding"); //TODO: details
			}
			isDeferred = false;
			deferred.clear();
		}		
	};
};

class PropertyBase {
public:
	std::unordered_set<ReactionBase*> dependentReactions = {};
};

void ReactionBase::addTriggeringProperty(PropertyBase* property) {
	triggeringProperties.insert(property);
	property->dependentReactions.insert(this);
}

void ReactionBase::unsubscribeFromTriggeringProperties() {
	if (triggeringProperties.size() == 0) return;
	for (auto triggeringProperty : triggeringProperties) {
		triggeringProperty->dependentReactions.erase(this);
	}
	triggeringProperties.clear();
}



template <typename T>
class Property:  PropertyBase,  ReactionBase {
public:
	using Function = std::function<const T&()>;
	//using FunctionThis = std::function<const T& (const Property& property)>;
private:
	T value = {};
	Function function = {};
	bool dirty = false;
	bool executionInProgress = false;
	std::unordered_set<ReactionBase*> reactionsWhoReceivedOldValue = {};
	
	T execute() {
		ValueGuard<ReactionBase*> g(Reaction::current);
		Reaction::current = this;

		unsubscribeFromTriggeringProperties();

		return function();
	}

public:	
	T getValue() {
		if (Reaction::current) {
			Reaction::current->addTriggeringProperty(this);
		}

		if (dirty) {
			if (executionInProgress) {
				reactionsWhoReceivedOldValue.insert(Reaction::current);
				return value;
			}

			T oldValue = value;
			executionInProgress = true;

			value = execute();

			executionInProgress = false;
			dirty = false;

			if (reactionsWhoReceivedOldValue.size() > 0) {

				if (value != oldValue) {
					Reaction::DeferredGuard _;
					for (auto reaction : reactionsWhoReceivedOldValue) {
						reaction->makeDirty();
					}
				}
				reactionsWhoReceivedOldValue.clear();
			}
		}
		return value;
	}

	void setFunction(Function function) {

		unsubscribeFromTriggeringProperties();
		this->function = function;

		Reaction::DeferredGuard _;
		makeDirty();

	}

	void setValue(const T& value) {
		unsubscribeFromTriggeringProperties();

		this->value = value;
		dirty = false;
		this->function = {};

		Reaction::DeferredGuard _;
		makeDependentReactionsDirty();
	}

	
private:
	virtual void makeDirty() override {
		if (dirty)
			return;

		dirty = true;
		makeDependentReactionsDirty();
	};

	void makeDependentReactionsDirty() {
		for (auto& reaction : dependentReactions) {
			reaction->makeDirty();
		}
	}

public:
	Property(const Property&) = delete;

	Property() {}

	Property(T value) {
		setValue(value);
	}

	Property(Function function) {
		setFunction(function);
	}	

	operator const T () {
		return getValue();
	}

	const void operator = (const T& value) {
		setValue(value);
	}
	const void operator = (Function function) {
		setFunction(function);
	}

};

template <typename T>
std::ostream& operator<<(std::ostream& stream, Property<T>& property){
	stream << (T)property;
	return stream;
}

#define Declarative(type, name, initializer)\
float Get##name() { return _##name.getValue(); }\
void Put##name(float value) { _##name.setValue(value); }\
__declspec(property(get = Get##name, put = Put##name)) float name;\
Property<type> _##name {initializer};\



//========================================================================================

#include <iostream>
class Test {
public:

private:
	Property<float> _A = 5;	

public:
	float GetA() { return _A.getValue(); } void PutA(float value) { _A.setValue(value); } __declspec(property(get = GetA, put = PutA)) float A;




	Declarative(float, E, [&]() {
		std::cout << "<E>";
		if (A == 0)
			return -1.0f;
		return A + 2;
		})

public:



	Property<float> B{[this]() {
		std::cout << "<B>";
		return A + E;
	}};

	Property<float> C{[this]() {
		std::cout << "<C>";
		return A + B;
	}};

	Property<float> D{ [this]() {
		std::cout << "<D>";
		return A + B + C;
	}};

};




int main() {

	Test test{};

	Reaction r3([&]() {
		std::cout << "test.D == " << test.D << std::endl;
		}
	);

	Reaction r2([&]() {
		std::cout << "test.C == " << test.C << std::endl;
		}
	);

	Reaction r1([&]() {
		std::cout << "test.B == " << test.B << std::endl;
		}
	);

	Reaction r0([&]() {
		std::cout << "test.A == " << test.A << std::endl;
		}
	);

	std::cout << ">>>>> test.A = 10" << std::endl;
	{
		Reaction::DeferredGuard _;
		test.A = 10;
	}

	std::cout << ">>>>> test.A = 0" << std::endl;
	{
		Reaction::DeferredGuard _;
		test.A = 0;
	}

	return 0;
}

