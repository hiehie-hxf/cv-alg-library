#pragma once

/** CV 领域组件契约。具体任务实现不得向业务层暴露推理后端专有类型。 */
namespace cvsdk {
class Preprocessor {
public:
  virtual ~Preprocessor() = default;
};
class DetectorComponent {
public:
  virtual ~DetectorComponent() = default;
};
class ClassifierComponent {
public:
  virtual ~ClassifierComponent() = default;
};
class SegmentorComponent {
public:
  virtual ~SegmentorComponent() = default;
};
class OcrComponent {
public:
  virtual ~OcrComponent() = default;
};
class MeasureComponent {
public:
  virtual ~MeasureComponent() = default;
};
class TrackerComponent {
public:
  virtual ~TrackerComponent() = default;
};
class Postprocessor {
public:
  virtual ~Postprocessor() = default;
};
} // namespace cvsdk
