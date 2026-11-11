// New Vegas Co-op by Zyrcxies (https://github.com/Zyrcxies/NewVegasCoop). Reusing this code? See LICENSE: credit and the same license are required.

#include "number_box.h"

#include "coop/gifts.h"
#include "game/game.h"
#include "log.h"

bool NumberBox::Start(const Setup& setup)
{
    Stop();
    m_setup = setup;
    m_value = Clamp(setup.start, setup.min, setup.max);
    if (!game::StartTextEntry())
        return false;
    if (!Ask())
    {
        game::StopTextEntry();
        return false;
    }
    m_open = true;
    return true;
}

bool NumberBox::Ask()
{
    int32_t now = m_value;
    m_shown = m_setup.text(now, "");
    std::vector<std::string> buttons{m_setup.confirm, m_setup.special.empty() ? std::string("--") : m_setup.special};
    for (int32_t step : kSteps)
        buttons.push_back((step > 0 ? "+" : "") + std::to_string(step));
    buttons.push_back(m_setup.last);
    return game::AskChoice(m_shown, buttons, m_setup.question);
}

void NumberBox::Stop()
{
    if (!m_open)
        return;
    m_open = false;
    game::StopTextEntry();
    game::DropQuestion(m_setup.question);
}

NumberBox::Result NumberBox::Update(int32_t& value)
{
    if (!m_open)
        return Result::Cancelled;
    game::EntryState state = game::TextEntryState();
    int box = game::TakeChoice(m_setup.question);
    std::string typed = state == game::EntryState::Typing ? game::TextEntryText() : std::string();
    int32_t now = m_value;
    bool number = Current(typed, m_value, m_setup, now);
    if (box != -1)
    {
        // A button: the box is gone, and comes back with the new number unless this ends it.
        if (box == 0)
        {
            Stop();
            if (!number)
                return Result::NotANumber;
            value = now;
            return Result::Confirmed;
        }
        if (box == kButtons - 1 || box < 0 || box >= kButtons)
        {
            Stop();
            return box == kButtons - 1 ? Result::Last : Result::Cancelled;
        }
        if (box == 1 && m_setup.special.empty())
            box = -2;
        m_value = Pressed(box, number ? now : m_value, m_setup);
        game::StopTextEntry();
        if (!game::StartTextEntry() || !Ask())
        {
            m_open = false;
            game::StopTextEntry();
            return Result::Cancelled;
        }
        return Result::Open;
    }
    if (state == game::EntryState::Typing)
    {
        std::string text = m_setup.text(now, typed);
        if (text != m_shown)
        {
            game::SetQuestionText(m_setup.question, text);
            m_shown = text;
        }
        return Result::Open;
    }
    // Enter or Esc in the typing hook.
    m_open = false;
    game::DropQuestion(m_setup.question);
    if (state != game::EntryState::Done)
    {
        game::StopTextEntry();
        return Result::Cancelled;
    }
    std::string done = game::TakeTextEntry();
    if (!Current(done, m_value, m_setup, now))
        return Result::NotANumber;
    value = now;
    return Result::Confirmed;
}
