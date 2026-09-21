#include "PluginEditor.h"

#include <cmath>
#include <iostream>
#include <thread>

AdbSynthAudioProcessorEditor::AdbSynthAudioProcessorEditor(AdbSynthAudioProcessor& audioProcessor)
    : AudioProcessorEditor(&audioProcessor), processor(audioProcessor)
{
    setSize(420, 660);

    const auto loadedFile = processor.getLoadedAudioFile();
    if (loadedFile.existsAsFile())
    {
        lastAudioDirectory = loadedFile.getParentDirectory();
        updateFolderFiles(lastAudioDirectory);
    }

    const auto& descriptor = adbsynth::parameterSchema[0];

    frequencyKnob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    frequencyKnob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 20);
    frequencyKnob.setRange(descriptor.minValue, descriptor.maxValue);
    frequencyKnob.setSkewFactorFromMidPoint(std::sqrt(descriptor.minValue * descriptor.maxValue));
    frequencyKnob.setNumDecimalPlacesToDisplay(2);
    frequencyKnob.setTextValueSuffix(" Hz");
    frequencyKnob.setTooltip(descriptor.label);
    addAndMakeVisible(frequencyKnob);

    frequencyAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters,
        descriptor.id,
        frequencyKnob);

    const auto& descriptor2 = adbsynth::parameterSchema[1];
    frequency2Knob.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    frequency2Knob.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 90, 20);
    frequency2Knob.setRange(descriptor2.minValue, descriptor2.maxValue);
    frequency2Knob.setSkewFactorFromMidPoint(std::sqrt(descriptor2.minValue * descriptor2.maxValue));
    frequency2Knob.setNumDecimalPlacesToDisplay(2);
    frequency2Knob.setTextValueSuffix(" Hz");
    frequency2Knob.setTooltip(descriptor2.label);
    addAndMakeVisible(frequency2Knob);

    frequency2Attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.parameters,
        descriptor2.id,
        frequency2Knob);

    chooseButton.onClick = [this] { chooseFile(); };
    guessButton.onClick = [this] { guessParameters(); };
    playFileButton.onClick = [this] { processor.triggerFilePlayback(); };
    playSynthButton.onStateChange = [this] { processor.setSynthHeld(playSynthButton.isDown()); };

    addAndMakeVisible(chooseButton);
    addAndMakeVisible(guessButton);
    addAndMakeVisible(playFileButton);
    addAndMakeVisible(playSynthButton);
    addAndMakeVisible(fileLabel);
    addAndMakeVisible(guessLabel);
    addAndMakeVisible(folderFilesList);

    fileLabel.setJustificationType(juce::Justification::centredLeft);
    guessLabel.setJustificationType(juce::Justification::centredLeft);
    fileLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    guessLabel.setColour(juce::Label::textColourId, juce::Colours::lightgrey);
    folderFilesList.setRowHeight(22);
    folderFilesList.setColour(juce::ListBox::backgroundColourId, juce::Colour(0xff1b242b));
    folderFilesList.setColour(juce::ListBox::outlineColourId, juce::Colour(0xff34434d));
    sourceOfTruthEditor.setMultiLine(true);
    sourceOfTruthEditor.setReadOnly(true);
    sourceOfTruthEditor.setScrollbarsShown(true);
    sourceOfTruthEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff1b242b));
    sourceOfTruthEditor.setColour(juce::TextEditor::textColourId, juce::Colours::lightgrey);
    addAndMakeVisible(sourceOfTruthEditor);
    sourceOfTruthEditor.setText("Source of truth unavailable", false);
    startTimerHz(10);
}

AdbSynthAudioProcessorEditor::~AdbSynthAudioProcessorEditor()
{
    stopTimer();
    guessRunning.store(false);
    if (guessThread.joinable())
        guessThread.join();
}

void AdbSynthAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(juce::Colour(0xff101418));
    auto waveformBounds = getLocalBounds().reduced(24);
    waveformBounds.setY(167);
    waveformBounds.setHeight(110);
    drawWaveform(graphics, waveformBounds);
}

void AdbSynthAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced(24);
    auto oscillatorRow = area.removeFromTop(135);
    frequencyKnob.setBounds(oscillatorRow.removeFromLeft(oscillatorRow.getWidth() / 2));
    frequency2Knob.setBounds(oscillatorRow);
    area.removeFromTop(8);
    area.removeFromTop(110);
    area.removeFromTop(8);
    auto modelRow = area.removeFromTop(30);
    chooseButton.setBounds(modelRow.removeFromLeft(125));
    modelRow.removeFromLeft(8);
    guessButton.setBounds(modelRow.removeFromLeft(125));
    fileLabel.setBounds(area.removeFromTop(22));
    guessLabel.setBounds(area.removeFromTop(22));
    area.removeFromTop(8);
    folderFilesList.setBounds(area.removeFromTop(110));
    area.removeFromTop(8);
    sourceOfTruthEditor.setBounds(area.removeFromTop(70));
    area.removeFromTop(8);
    playFileButton.setBounds(area.removeFromTop(34).removeFromLeft(125));
    playSynthButton.setBounds(area.removeFromTop(34).removeFromLeft(125));
}

void AdbSynthAudioProcessorEditor::timerCallback()
{
    const auto file = processor.getLoadedAudioFile();
    fileLabel.setText(file.existsAsFile() ? file.getFileName() : "No audio file loaded", juce::dontSendNotification);
    if (file != sourceOfTruthForFile)
    {
        sourceOfTruthForFile = file;
        updateSourceOfTruth(file);
    }
    repaint();
}

void AdbSynthAudioProcessorEditor::updateSourceOfTruth(const juce::File& file)
{
    if (!file.existsAsFile())
    {
        sourceOfTruthEditor.setText("Source of truth unavailable", false);
        return;
    }

    const auto labelsFile = juce::File(ADBSYNTH_PROJECT_DIR).getChildFile(".tmp/ml/train/labels.jsonl");
    juce::FileInputStream stream(labelsFile);
    if (!stream.openedOk())
    {
        sourceOfTruthEditor.setText("Source of truth unavailable", false);
        return;
    }

    juce::var label;
    while (!stream.isExhausted())
    {
        const auto line = stream.readNextLine();
        juce::var candidate;
        if (juce::JSON::parse(line, candidate).wasOk() && candidate.isObject()
            && candidate.getDynamicObject()->getProperty("file").toString() == file.getFileName())
        {
            label = candidate;
            break;
        }
    }

    if (!label.isObject())
    {
        sourceOfTruthEditor.setText("Source of truth unavailable", false);
        return;
    }

    auto parameters = std::make_unique<juce::DynamicObject>();
    for (const auto& descriptor : adbsynth::parameterSchema)
        parameters->setProperty(descriptor.id, label.getDynamicObject()->getProperty(descriptor.id));

    sourceOfTruthEditor.setText(juce::JSON::toString(juce::var(parameters.release()), true), false);
}

void AdbSynthAudioProcessorEditor::chooseFile()
{
    auto chooser = std::make_shared<juce::FileChooser>("Select audio file", lastAudioDirectory,
                                                       "*.wav;*.aif;*.aiff;*.flac;*.ogg");
    chooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                         [this, chooser] (const juce::FileChooser& result)
                         {
                             const auto file = result.getResult();
                             if (file.existsAsFile())
                                 loadSelectedFile(file);
                         });
}

int AdbSynthAudioProcessorEditor::getNumRows()
{
    return folderFiles.size();
}

void AdbSynthAudioProcessorEditor::paintListBoxItem(int rowNumber, juce::Graphics& graphics,
                                                    int width, int height, bool rowIsSelected)
{
    if (!juce::isPositiveAndBelow(rowNumber, folderFiles.size()))
        return;

    graphics.fillAll(rowIsSelected ? juce::Colour(0xff315c54) : juce::Colour(0xff1b242b));
    graphics.setColour(juce::Colours::lightgrey);
    graphics.drawText(folderFiles.getReference(rowNumber).getFileName(), 8, 0, width - 16, height,
                      juce::Justification::centredLeft, true);
}

void AdbSynthAudioProcessorEditor::listBoxItemClicked(int row, const juce::MouseEvent&)
{
    if (juce::isPositiveAndBelow(row, folderFiles.size()))
        loadSelectedFile(folderFiles.getReference(row));
}

void AdbSynthAudioProcessorEditor::updateFolderFiles(const juce::File& directory)
{
    folderFiles.clear();
    if (directory.isDirectory())
    {
        directory.findChildFiles(folderFiles, juce::File::findFiles, false,
                                 "*.wav;*.aif;*.aiff;*.flac;*.ogg");
        struct FileComparator
        {
            int compareElements(const juce::File& first, const juce::File& second) const
            {
                return first.getFileName().compareNatural(second.getFileName());
            }
        };
        FileComparator compareFiles;
        folderFiles.sort(compareFiles);
    }
    folderFilesList.updateContent();
}

void AdbSynthAudioProcessorEditor::loadSelectedFile(const juce::File& file)
{
    lastAudioDirectory = file.getParentDirectory();
    updateFolderFiles(lastAudioDirectory);
    if (!processor.loadAudioFile(file))
        guessLabel.setText("Could not load that audio file.", juce::dontSendNotification);
}

void AdbSynthAudioProcessorEditor::guessParameters()
{
    const auto file = processor.getLoadedAudioFile();
    if (!file.existsAsFile() || guessRunning.exchange(true))
        return;

    if (guessThread.joinable())
        guessThread.join();

    guessLabel.setText("Guessing...", juce::dontSendNotification);
    juce::Component::SafePointer<AdbSynthAudioProcessorEditor> safeThis(this);
    guessThread = std::thread([this, file, safeThis]
                              {
                                  juce::String output, error;
                                  const auto success = processor.runModelGuess(file, output, error);
                                  juce::MessageManager::callAsync([safeThis, success, output, error]
                                  {
                                      if (safeThis == nullptr)
                                          return;

                                      safeThis->guessRunning.store(false);
                                      safeThis->updateGuess(success ? output : juce::String {}, success ? juce::String {} : error);
                                  });
                              });
}

void AdbSynthAudioProcessorEditor::updateGuess(const juce::String& output, const juce::String& error)
{
    if (error.isNotEmpty())
    {
        guessLabel.setText(error, juce::dontSendNotification);
        return;
    }

    juce::var parsed;
    if (juce::JSON::parse(output, parsed).failed() || !parsed.isObject())
    {
        std::cerr << "[AdbSynth] Model returned invalid output:\n---\n"
                  << output.toStdString()
                  << "\n---" << std::endl;
        guessLabel.setText("The model returned invalid output.", juce::dontSendNotification);
        return;
    }

    bool updated = false;
    for (const auto& descriptor : adbsynth::parameterSchema)
    {
        const auto value = parsed.getDynamicObject()->getProperty(descriptor.id);
        if (value.isDouble() || value.isInt())
        {
            auto* parameter = const_cast<juce::RangedAudioParameter*>(processor.parameters.getParameter(descriptor.id));
            parameter->setValueNotifyingHost(parameter->convertTo0to1(static_cast<float>(value)));
            updated = true;
        }
    }

    if (updated)
        guessLabel.setText("Guessed oscillator frequencies.", juce::dontSendNotification);
}

void AdbSynthAudioProcessorEditor::drawWaveform(juce::Graphics& graphics, juce::Rectangle<int> bounds) const
{
    graphics.setColour(juce::Colour(0xff1b242b));
    graphics.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    graphics.setColour(juce::Colour(0xff4fbd91));

    const auto values = processor.getWaveform();
    if (values.empty())
        return;

    juce::Path path;
    const auto centre = bounds.getCentreY();
    path.startNewSubPath(static_cast<float>(bounds.getX()), static_cast<float>(centre));

    for (int index = 0; index < static_cast<int>(values.size()); ++index)
    {
        const auto x = juce::jmap(static_cast<float>(index), 0.0f, static_cast<float>(values.size() - 1),
                                  static_cast<float>(bounds.getX()), static_cast<float>(bounds.getRight()));
        path.lineTo(x, static_cast<float>(centre) - values[static_cast<size_t>(index)] * bounds.getHeight() * 0.45f);
    }

    graphics.strokePath(path, juce::PathStrokeType(1.5f));
}