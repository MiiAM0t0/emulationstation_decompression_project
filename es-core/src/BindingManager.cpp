#include "BindingManager.h"
#include "utils/StringUtil.h"
#include "LocaleES.h"
#include <time.h>

#include "components/TextComponent.h"
#include "components/ImageComponent.h"
#include "components/VideoComponent.h"
#include "components/StackPanelComponent.h"

#include "utils/Platform.h"
#include "SystemConf.h"
#include "utils/MathExpr.h"

BindableProperty BindableProperty::Null;
BindableProperty BindableProperty::EmptyString("", BindablePropertyType::String);

class GlobalBinding : public IBindable
{
	BindableProperty getProperty(const std::string& name) override
	{
		if (name == "help")
			return Settings::getInstance()->getBool("ShowHelpPrompts");

		if (name == "clock")
			return Settings::DrawClock();

		if (name == "architecture")
			return Utils::Platform::getArchString();

		if (name == "cheevos")
			return SystemConf::getInstance()->getBool("global.retroachievements");

		if (name == "cheevosUser")
			return SystemConf::getInstance()->getBool("global.retroachievements") ? SystemConf::getInstance()->get("global.retroachievements.username") : "";

		if (name == "netplay")
			return SystemConf::getInstance()->getBool("global.netplay") ? "true" : "false";

		if (name == "netplay.username")
			return SystemConf::getInstance()->getBool("global.netplay") ? SystemConf::getInstance()->get("global.netplay.nickname") : "";

		if (name == "ip")
			return Utils::Platform::queryIPAdress();

		if (name == "network")
			return !Utils::Platform::queryIPAdress().empty();

		if (name == "battery")
			return Utils::Platform::queryBatteryInformation().hasBattery;

		if (name == "batteryLevel")
			return Utils::Platform::queryBatteryInformation().level;

		if (name == "screenWidth" || name == "width")
			return Renderer::getScreenWidth();

		if (name == "screenHeight" || name == "height")
			return Renderer::getScreenHeight();

		if (name == "screenRatio" || name == "ratio")
			return Renderer::getAspectRatio();

		if (name == "vertical")
			return  Renderer::isVerticalScreen();

		return BindableProperty::Null;
	}

	std::string getBindableTypeName() override { return "global"; }
};

static GlobalBinding globalBinding;

void BindingManager::bindValues(IBindable* current, std::string& xp, bool showDefaultText, std::string& evaluableExpression)
{
	std::string typeName = current->getBindableTypeName();

	for (auto name : Utils::String::extractStrings(xp, "{" + typeName + ":", "}"))
	{
		std::string dataAsString;
		std::string dataAsEvaluable;

		IBindable* root = current;
		std::string propertyName = name;

		for (auto propName : Utils::String::split(name, ':', true))
		{
			propertyName = propName;

			auto value = root->getProperty(propName);
			if (value.type != BindablePropertyType::Bindable || value.bindable == nullptr)
				break;

			propertyName = "name"; // use default "name" property for IBinding if not property specified later
			root = value.bindable;
		}

		auto value = root->getProperty(propertyName);
		switch (value.type)
		{
		case BindablePropertyType::String:
		case BindablePropertyType::Path:
			dataAsString = value.s;
			dataAsEvaluable = "\"" + Utils::String::replace(value.s, "\"", "") + "\""; // Should be managed differenty
			break;
		case BindablePropertyType::Bool:
			dataAsString = value.b ? _("YES") : _("NO");
			dataAsEvaluable = value.b ? "1" : "0";
			break;
		case BindablePropertyType::Int:
			dataAsString = std::to_string(value.i);
			dataAsEvaluable = dataAsString;
			break;
		case BindablePropertyType::Float:
			dataAsString = std::to_string(value.f);
			dataAsEvaluable = dataAsString;
			break;
		}

		if (showDefaultText && value.type != BindablePropertyType::Path)
			dataAsString = dataAsString.empty() ? _("Unknown") : dataAsString == "0" ? _("None") : dataAsString;

		xp = Utils::String::replace(xp, "{" + typeName + ":" + name + "}", dataAsString);
		evaluableExpression = Utils::String::replace(evaluableExpression, "{" + typeName + ":" + name + "}", dataAsEvaluable);
	}
}

std::string BindingManager::updateBoundExpression(std::string& xp, IBindable* bindable, bool showDefaultText)
{
	std::string evaluableExpression = xp;

	if (bindable == nullptr)
	{
		for (auto name : Utils::String::extractStrings(xp, "{", "}"))
			if (name.find(":") != std::string::npos)
				xp = Utils::String::replace(xp, "{" + name + "}", "");
	}
	else
	{
		xp = Utils::String::replace(xp, "{binding:", "{system:"); // Retrocompatibility for old {binding: which is {system
		evaluableExpression = xp;

		IBindable* current = bindable;

		while (current != nullptr)
		{
			bindValues(current, xp, showDefaultText, evaluableExpression);
			current = current->getBindableParent();
		}
	}

	bindValues(&globalBinding, xp, showDefaultText, evaluableExpression);
	return evaluableExpression;
}

void BindingManager::updateBindings(GuiComponent* comp, IBindable* bindable, bool recursive)
{
	if (comp == nullptr || comp->getExtraType() == ExtraType::BUILTIN)
		return;

	TextComponent* text = dynamic_cast<TextComponent*>(comp);	
	bool showDefaultText = text != nullptr && text->getBindingDefaults();

	auto expressions = comp->getBindingExpressions();
	for (auto expression : expressions)
	{
		std::string xp = expression.second;
		if (xp.empty())
			continue;
		
		std::string propertyName = expression.first;

		auto existing = comp->getProperty(propertyName);
		if (existing.type == ThemeData::ThemeElement::Property::PropertyType::Unknown)
			continue;

		bool uniqueVariable = xp[0] == '{' && xp[xp.size() - 1] == '}' && Utils::String::occurs(xp, '{') == 1;

		std::string evaluableExpression = updateBoundExpression(xp, bindable, text != nullptr && showDefaultText);
		
		switch (existing.type)
		{
		case ThemeData::ThemeElement::Property::PropertyType::String:
			
			if (bindable != nullptr && !uniqueVariable)
			{
				try
				{
					auto ret = Utils::MathExpr::evaluate(evaluableExpression.c_str());
					if (ret.type == Utils::MathExpr::STRING)
						xp = ret.string;
				}
				catch (...) { }
			}			

			comp->setProperty(propertyName, Utils::String::trim(xp));
			break;
		case ThemeData::ThemeElement::Property::PropertyType::Int:
			{
				int value = Utils::String::toInteger(xp);

				if (xp != "0" && xp != "1" && bindable != nullptr && !uniqueVariable)
				{
					try
					{
						auto ret = Utils::MathExpr::evaluate(evaluableExpression.c_str());
						if (ret.type == Utils::MathExpr::NUMBER)
							value = (int)ret.number;
					}
					catch (...) { }
				}

				comp->setProperty(propertyName, (unsigned int)value);
			}
			
			break;
		case ThemeData::ThemeElement::Property::PropertyType::Float:
			{
				float value = Utils::String::toFloat(xp);

				if (bindable != nullptr && !uniqueVariable)
				{
					try
					{
						auto ret = Utils::MathExpr::evaluate(evaluableExpression.c_str());
						if (ret.type == Utils::MathExpr::NUMBER)
							value = ret.number;
					}
					catch (...) { }
				}

				comp->setProperty(propertyName, value);
			}
			break;
		case ThemeData::ThemeElement::Property::PropertyType::Bool:
		{
			if (evaluableExpression == "1")
				comp->setProperty(propertyName, true);
			else if (evaluableExpression == "0")
				comp->setProperty(propertyName, false);
			else 
			{
				bool value = false;

				if (bindable != nullptr && !uniqueVariable)
				{
					try
					{
						auto ret = Utils::MathExpr::evaluate(evaluableExpression.c_str());
						if (ret.type == Utils::MathExpr::NUMBER)
							value = (ret.number != 0);
					}
					catch (...) { }
				}

				comp->setProperty(propertyName, value); // negate ? !value : value);
			}
		}
		break;
		}
	}

	// Storyboards. Manage bindings on 'enabled' property
	for (auto storyBoards : comp->mStoryBoards)
	{
		for (auto anim : storyBoards.second->animations)
		{
			if (anim->enabledExpression.empty())
				continue;
			
			std::string xp = anim->enabledExpression;
			std::string evaluableExpression = updateBoundExpression(xp, bindable, text != nullptr && showDefaultText);
			
			bool value = false;

			if (bindable != nullptr)
			{
				try
				{
					auto ret = Utils::MathExpr::evaluate(evaluableExpression.c_str());
					if (ret.type == Utils::MathExpr::NUMBER)
						value = (ret.number != 0);
				}
				catch (...) { }
			}

			anim->enabled = value;
		}

	}

	if (recursive)
	{
		for (int i = 0; i < comp->getChildCount(); i++)
			updateBindings(comp->getChild(i), bindable, recursive);

		StackPanelComponent* stack = dynamic_cast<StackPanelComponent*>(comp);
		if (stack != nullptr)
			stack->onSizeChanged();
	}

	
}

ComponentBinding::ComponentBinding(GuiComponent* comp, IBindable* parent)
{
	mParent = parent;
	mComponent = comp;
	mName = mComponent->getTag();
}

BindableProperty ComponentBinding::getProperty(const std::string& name)
{
	auto prop = mComponent->getProperty(name);

	switch (prop.type)
	{
	case ThemeData::ThemeElement::Property::PropertyType::String:
		return prop.s;
	case ThemeData::ThemeElement::Property::PropertyType::Int:
		return (int)prop.i;
	case ThemeData::ThemeElement::Property::PropertyType::Bool:
		return (int)prop.b;
	case ThemeData::ThemeElement::Property::PropertyType::Float:
		return prop.f;
	}

	return BindableProperty::Null;
}

std::string BindableProperty::toString()
{
	switch (type)
	{
	case BindablePropertyType::String:
	case BindablePropertyType::Path:
		return s;
	case BindablePropertyType::Int:
		return std::to_string(i);
	case BindablePropertyType::Bool:
		return b ? "true" : "false";
	case BindablePropertyType::Float:
		return std::to_string(f);
	}

	return "";
}